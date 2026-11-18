"""Coarse-to-fine Artist Layer Grammar + PyTorch LPIPS/DISTS optimizer -> C++ compile-plan."""
from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
import subprocess
import tempfile

import cv2
import numpy as np
from PIL import Image

try:
    import torch
    import torch.nn.functional as F
    from .diff_renderer import DifferentiableStrokeField, linear_to_srgb_torch, srgb_to_linear_torch
except ImportError:  # pragma: no cover - lightweight non-PyTorch environments
    torch = None  # type: ignore[assignment]
    F = None  # type: ignore[assignment]
    DifferentiableStrokeField = None  # type: ignore[assignment]
    linear_to_srgb_torch = None  # type: ignore[assignment]
    srgb_to_linear_torch = None  # type: ignore[assignment]

from ..manifest import load_manifest
from .saliency import compute_lab_etf, compute_semantic_roi_weights


@dataclass
class DifferentiableArtistConfig:
    steps: int = 36
    lr_points: float = 0.40
    lr_widths: float = 0.05
    lr_color: float = 0.07
    lr_opacity: float = 0.07
    max_strokes_per_object: int = 180
    amodal_background: bool = True
    use_perceptual_loss: bool = True
    target_size: tuple[int, int] | None = None
    target_dpi: float = 300.0
    cpp_residual_pass: bool = True


def _srgb_u8_to_lin_np(rgb_u8: np.ndarray) -> np.ndarray:
    s = np.clip(rgb_u8.astype(np.float32) / 255.0, 0.0, 1.0)
    return np.where(s <= 0.04045, s / 12.92, np.power((s + 0.055) / 1.055, 2.4)).astype(np.float32)


def _lin_to_srgb_01_np(lin: np.ndarray) -> np.ndarray:
    l = np.clip(lin, 0.0, 1.0)
    return np.where(l <= 0.0031308, l * 12.92, 1.055 * np.power(np.maximum(l, 1e-8), 1.0 / 2.4) - 0.055).astype(np.float32)


def _build_perceptual_losses(device: torch.device):
    lpips_fn = None
    dists_fn = None
    try:
        import lpips
        lpips_fn = lpips.LPIPS(net='alex', verbose=False).to(device).eval()
        for p in lpips_fn.parameters():
            p.requires_grad_(False)
    except Exception:
        lpips_fn = None
    try:
        from DISTS_pytorch import DISTS
        dists_fn = DISTS().to(device).eval()
        for p in dists_fn.parameters():
            p.requires_grad_(False)
    except Exception:
        dists_fn = None
    return lpips_fn, dists_fn


def _inpaint_amodal_background(rgb_u8: np.ndarray, bg_mask_u8: np.ndarray) -> np.ndarray:
    """Inpaint foreground-occluded areas in background purely in memory with 4px halo erosion."""
    occluded = (bg_mask_u8 == 0).astype(np.uint8)
    if not np.any(occluded):
        return rgb_u8.copy()
    # Dilate occluded mask by 4px so uncertain segmentation rims never bleed into background
    kern = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    inpaint_mask = cv2.dilate(occluded, kern, iterations=1)
    if np.all(inpaint_mask > 0):
        inpaint_mask = occluded
    bgr = cv2.cvtColor(rgb_u8, cv2.COLOR_RGB2BGR)
    filled_bgr = cv2.inpaint(bgr, inpaint_mask, inpaintRadius=9.0, flags=cv2.INPAINT_TELEA)
    smooth_bgr = cv2.GaussianBlur(filled_bgr, (21, 21), 6.0)
    alpha = cv2.GaussianBlur(inpaint_mask.astype(np.float32), (15, 15), 4.0)[..., None]
    blended = filled_bgr.astype(np.float32) * (1.0 - 0.65 * alpha) + smooth_bgr.astype(np.float32) * (0.65 * alpha)
    out_rgb = cv2.cvtColor(np.clip(blended, 0, 255).astype(np.uint8), cv2.COLOR_BGR2RGB)
    keep = inpaint_mask == 0
    out_rgb[keep] = rgb_u8[keep]
    return out_rgb


def _chaikin_smooth_polygon(pts: np.ndarray, iterations: int = 2) -> np.ndarray:
    """Corner-aware Chaikin curve refinement for crisp super-resolution outer silhouettes."""
    if len(pts) < 4:
        return pts.astype(np.float32)
    cur = pts.astype(np.float32)
    for _ in range(iterations):
        n = len(cur)
        nxt = []
        for i in range(n):
            p0 = cur[(i - 1) % n]
            p1 = cur[i]
            p2 = cur[(i + 1) % n]
            v1 = p1 - p0
            v2 = p2 - p1
            l1 = float(np.hypot(v1[0], v1[1]))
            l2 = float(np.hypot(v2[0], v2[1]))
            cos_a = float(np.dot(v1, v2) / max(l1 * l2, 1e-6))
            if cos_a < 0.25:
                nxt.append(p1)
            else:
                nxt.append(0.75 * p1 + 0.25 * p2)
                nxt.append(0.25 * p1 + 0.75 * p2)
        cur = np.asarray(nxt, dtype=np.float32)
        if len(cur) > 1200:
            break
    return cur


def _extract_spans_from_mask(binary_mask: np.ndarray) -> list[list[int]]:
    ys, _ = np.nonzero(binary_mask)
    if len(ys) == 0:
        return []
    spans: list[list[int]] = []
    for y in range(int(ys.min()), int(ys.max()) + 1):
        row = binary_mask[y]
        if not np.any(row):
            continue
        padded = np.concatenate([[False], row, [False]])
        diffs = np.diff(padded.astype(np.int8))
        starts = np.where(diffs == 1)[0]
        ends = np.where(diffs == -1)[0]
        for s0, e0 in zip(starts, ends):
            spans.append([int(s0), int(e0), int(y)])
    return spans


def _extract_flat_polygons_and_canvas(
    rgb_u8: np.ndarray,
    mask_bool: np.ndarray,
    roi_weights: np.ndarray | None = None,
    max_clusters: int = 40,
    use_polygons_for_silhouette: bool = False,
) -> tuple[list[dict], np.ndarray]:
    """Extract gap-free flat color zones + error-gated antialiased transitions without step-edge halos."""
    h, w = mask_bool.shape
    flat_canvas = np.zeros((h, w, 4), dtype=np.uint8)
    if not np.any(mask_bool):
        return [], flat_canvas

    bilat = cv2.bilateralFilter(rgb_u8, d=5, sigmaColor=18, sigmaSpace=5)
    active_rgb = bilat[mask_bool].astype(np.float32)
    n_px = len(active_rgb)
    k = max(2, min(max_clusters, int(math.sqrt(n_px / 180.0)) + 4))
    if n_px < k * 8:
        k = 1

    if k == 1:
        mean_c = active_rgb.mean(axis=0)
        centers = np.asarray([mean_c], dtype=np.float32)
        labels_full = np.full((h, w), -1, dtype=np.int32)
        labels_full[mask_bool] = 0
    else:
        rng = np.random.default_rng(1337 + n_px % 997)
        n_samples = min(n_px, 8000)
        if roi_weights is not None:
            w_act = np.maximum(1e-3, roi_weights[mask_bool].astype(np.float64)) ** 2
            w_prob = w_act / w_act.sum()
            n_sal = n_samples // 2
            n_uni = n_samples - n_sal
            idx_sal = rng.choice(n_px, size=n_sal, replace=(n_px < n_sal), p=w_prob)
            idx_uni = np.linspace(0, n_px - 1, n_uni, dtype=np.int64)
            samples = np.concatenate([active_rgb[idx_sal], active_rgb[idx_uni]], axis=0)
        else:
            idx_uni = np.linspace(0, n_px - 1, n_samples, dtype=np.int64)
            samples = active_rgb[idx_uni]

        init_centers = [samples.mean(axis=0)]
        for _ in range(1, k):
            c_arr = np.stack(init_centers, axis=0)
            min_d2 = ((samples[:, None, :] - c_arr[None, :, :]) ** 2).sum(axis=2).min(axis=1)
            init_centers.append(samples[int(np.argmax(min_d2))].copy())
        centers = np.stack(init_centers, axis=0).astype(np.float32)
        for _ in range(10):
            d2 = ((samples[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
            assign = np.argmin(d2, axis=1)
            for ci in range(k):
                sel = samples[assign == ci]
                if len(sel) > 0:
                    centers[ci] = sel.mean(axis=0)

        dists = ((active_rgb[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        nearest = np.argmin(dists, axis=1).astype(np.int32)
        labels_full = np.full((h, w), -1, dtype=np.int32)
        labels_full[mask_bool] = nearest

        for ci in range(len(centers)):
            cm = (labels_full == ci).astype(np.uint8)
            n_cc, cc_map, stats, _ = cv2.connectedComponentsWithStats(cm, connectivity=8)
            if n_cc <= 1:
                continue
            tiny = np.zeros((h, w), dtype=bool)
            for cc_i in range(1, n_cc):
                if stats[cc_i, cv2.CC_STAT_AREA] < 4:
                    tiny |= (cc_map == cc_i)
            if np.any(tiny):
                other_mask = np.arange(len(centers)) != ci
                if np.any(other_mask):
                    other_ids = np.nonzero(other_mask)[0]
                    sub_d = ((bilat[tiny].astype(np.float32)[:, None, :] - centers[other_ids][None, :, :]) ** 2).sum(axis=2)
                    labels_full[tiny] = other_ids[np.argmin(sub_d, axis=1)]

    strokes: list[dict] = []
    counts = [int(np.sum(labels_full == ci)) for ci in range(len(centers))]
    med_rgb = np.median(active_rgb, axis=0)
    valid_cis = [ci for ci in range(len(centers)) if counts[ci] >= max(8, int(0.02 * n_px))] or list(range(len(centers)))
    dom_k = min(valid_cis, key=lambda ci: float(((centers[ci] - med_rgb) ** 2).sum()))

    dom_rgb = [float(np.clip(centers[dom_k][c] / 255.0, 0.0, 1.0)) for c in range(3)]
    if use_polygons_for_silhouette:
        sil_u8 = mask_bool.astype(np.uint8) * 255
        contours, hierarchy = cv2.findContours(sil_u8, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
        rem_mask = mask_bool.copy()
        if contours and hierarchy is not None:
            hier = hierarchy[0]
            for idx, cnt in enumerate(contours):
                if hier[idx][3] == -1 and hier[idx][2] == -1 and cv2.contourArea(cnt) >= 160.0 and len(cnt) >= 4:
                    approx = cv2.approxPolyDP(cnt, 0.42, True)[:, 0, :].astype(np.float32) + 0.5
                    smooth_poly = _chaikin_smooth_polygon(approx, iterations=2)
                    if len(smooth_poly) >= 3:
                        strokes.append({
                            'engine': 8,
                            'tool': 'bucket',
                            'color': [dom_rgb[0], dom_rgb[1], dom_rgb[2], 1.0],
                            'linearOptical': True,
                            'polygon': [[round(float(p[0]), 3), round(float(p[1]), 3)] for p in smooth_poly],
                        })
                        drawn = np.zeros((h, w), dtype=np.uint8)
                        cv2.drawContours(drawn, contours, idx, 255, thickness=cv2.FILLED)
                        rem_mask &= (drawn == 0)
        rem_spans = _extract_spans_from_mask(rem_mask)
        if rem_spans:
            strokes.append({
                'engine': 8,
                'tool': 'bucket',
                'color': [dom_rgb[0], dom_rgb[1], dom_rgb[2], 1.0],
                'linearOptical': True,
                'spans': rem_spans,
            })
    else:
        base_spans = _extract_spans_from_mask(mask_bool)
        if base_spans:
            strokes.append({
                'engine': 8,
                'tool': 'bucket',
                'color': [dom_rgb[0], dom_rgb[1], dom_rgb[2], 1.0],
                'linearOptical': True,
                'spans': base_spans,
            })

    flat_canvas[mask_bool, :3] = np.clip(np.round(centers[dom_k]), 0, 255).astype(np.uint8)
    flat_canvas[mask_bool, 3] = 255

    # Emit exact pixel-partitioned spans for non-dominant clusters (ordered large -> focal)
    # so adjacent cel regions NEVER shrink apart or leak base skin color!
    other_order = sorted(
        [ci for ci in range(len(centers)) if ci != dom_k and counts[ci] >= 4],
        key=lambda ci: counts[ci],
        reverse=True,
    )
    for ci in other_order:
        c_bool = labels_full == ci
        c_spans = _extract_spans_from_mask(c_bool)
        if not c_spans:
            continue
        rgb01 = [float(np.clip(centers[ci][c] / 255.0, 0.0, 1.0)) for c in range(3)]
        strokes.append({
            'engine': 8,
            'tool': 'bucket',
            'color': [rgb01[0], rgb01[1], rgb01[2], 1.0],
            'linearOptical': True,
            'spans': c_spans,
        })
        flat_canvas[c_bool, :3] = np.clip(np.round(centers[ci]), 0, 255).astype(np.uint8)
        flat_canvas[c_bool, 3] = 255

    # Phase B: Simultaneous two-sided error-gated Linear-sRGB transition spans AFTER all opaque
    # clusters are placed (prevents later clusters from erasing earlier transition spans and includes
    # dom_k, melting smooth gradient bands while keeping crisp cel step edges razor-sharp!).
    if len(centers) >= 2 and np.any(mask_bool):
        inner_mask = (
            mask_bool
            if np.all(mask_bool)
            else (cv2.erode(mask_bool.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0)
        )
        zy, zx = np.nonzero(inner_mask if np.any(inner_mask) else mask_bool)
        base_k = labels_full[zy, zx]
        cur_rgb = flat_canvas[zy, zx, :3]
        tgt_rgb_f = rgb_u8[zy, zx].astype(np.float32)
        old_err = ((cur_rgb.astype(np.float32) - tgt_rgb_f) ** 2).sum(axis=-1)
        dst_lin = _srgb_u8_to_lin_np(cur_rgb)
        centers_u8 = np.clip(np.round(centers), 0, 255).astype(np.uint8)
        pal_lin = _srgb_u8_to_lin_np(centers_u8[:, None, :])[:, 0, :]

        # Gather unique neighbor candidate clusters via multi-scale spatial taps
        tap_offsets = (-22, -10, -3, 0, 3, 10, 22) if max(h, w) >= 900 else (-14, -6, -2, 0, 2, 6, 14)
        cand_set: list[np.ndarray] = []
        for dy in tap_offsets:
            yy = np.clip(zy + dy, 0, h - 1)
            for dx in tap_offsets:
                if dx == 0 and dy == 0:
                    continue
                xx = np.clip(zx + dx, 0, w - 1)
                cand_set.append(labels_full[yy, xx])
        cand_mat = np.stack(cand_set, axis=1)  # [N_px, N_taps]

        best_err = old_err - 3.0
        win_k = np.full(len(zy), -1, dtype=np.int32)
        win_tier = np.full(len(zy), -1, dtype=np.int32)
        win_rgb = cur_rgb.copy()
        tier_alphas = (0.16, 0.32, 0.48, 0.64)

        for ci in range(len(centers)):
            if counts[ci] < 4:
                continue
            has_ci = (base_k != ci) & np.any(cand_mat == ci, axis=1) & (best_err > 0.0)
            if not np.any(has_ci):
                continue
            p_idx = np.nonzero(has_ci)[0]
            src_c_lin = pal_lin[ci]
            d_lin_sub = dst_lin[p_idx]
            tgt_sub = tgt_rgb_f[p_idx]
            for t_idx, alpha_t in enumerate(tier_alphas):
                mix_lin = alpha_t * src_c_lin[None, :] + (1.0 - alpha_t) * d_lin_sub
                mix_u8 = np.clip(np.round(_lin_to_srgb_01_np(mix_lin) * 255.0), 0, 255).astype(np.uint8)
                err_t = ((mix_u8.astype(np.float32) - tgt_sub) ** 2).sum(axis=-1)
                better = err_t < best_err[p_idx]
                if np.any(better):
                    upd = p_idx[better]
                    best_err[upd] = err_t[better]
                    win_k[upd] = ci
                    win_tier[upd] = t_idx
                    win_rgb[upd] = mix_u8[better]

        for ci in range(len(centers)):
            if counts[ci] < 4:
                continue
            rgb01 = [float(np.clip(centers[ci][c] / 255.0, 0.0, 1.0)) for c in range(3)]
            for t_idx, alpha_t in enumerate(tier_alphas):
                sel = (win_k == ci) & (win_tier == t_idx)
                if not np.any(sel):
                    continue
                fringe = np.zeros((h, w), dtype=bool)
                fringe[zy[sel], zx[sel]] = True
                aa_spans = _extract_spans_from_mask(fringe)
                if aa_spans:
                    strokes.append({
                        'engine': 8,
                        'tool': 'bucket',
                        'color': [rgb01[0], rgb01[1], rgb01[2], alpha_t],
                        'linearOptical': True,
                        'spans': aa_spans,
                    })
                    flat_canvas[zy[sel], zx[sel], :3] = win_rgb[sel]

    return strokes, flat_canvas


def _seed_etf_strokes(
    rgb_u8: np.ndarray,
    flat_rgb_u8: np.ndarray,
    mask_bool: np.ndarray,
    dist_inside: np.ndarray,
    roi_weights: np.ndarray,
    tx: np.ndarray,
    ty: np.ndarray,
    max_strokes: int,
    n_ctrl: int = 6,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, list[str], list[str]]:
    """Seed artist streamlines with boundary clearance, macro soft washes, and XDoG ridge ink."""
    h, w = mask_bool.shape
    bilat_rgb = cv2.bilateralFilter(rgb_u8, d=5, sigmaColor=18, sigmaSpace=5)
    diff_sq = ((rgb_u8.astype(np.float32) - flat_rgb_u8.astype(np.float32)) ** 2).mean(axis=2)
    smooth_diff_sq = ((bilat_rgb.astype(np.float32) - flat_rgb_u8.astype(np.float32)) ** 2).mean(axis=2)
    score_map = diff_sq * roi_weights * mask_bool.astype(np.float32)

    gray = cv2.cvtColor(rgb_u8, cv2.COLOR_RGB2GRAY).astype(np.float32)
    grad_x = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3) * 0.25
    grad_y = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3) * 0.25
    grad_mag = np.hypot(grad_x, grad_y)

    blur_s = cv2.GaussianBlur(gray, (3, 3), 0.8)
    blur_l = cv2.GaussianBlur(gray, (9, 9), 2.4)
    xdog_dark = np.maximum(0.0, blur_l - blur_s) * mask_bool.astype(np.float32)

    pts_list: list[np.ndarray] = []
    w_list: list[np.ndarray] = []
    rgb_list: list[np.ndarray] = []
    op_list: list[float] = []
    soft_list: list[float] = []
    modes: list[str] = []
    stages: list[str] = []

    def trace_poly(
        sx: int,
        sy: int,
        step_len: float,
        half_steps: int,
        min_clear: float,
        max_jump_sq: float = 900.0,
    ) -> np.ndarray | None:
        cx, cy = float(sx) + 0.5, float(sy) + 0.5
        d0x, d0y = float(tx[sy, sx]), float(ty[sy, sx])
        seed_c = bilat_rgb[sy, sx].astype(np.float32)
        arms: list[list[tuple[float, float]]] = [[], []]
        for side in range(2):
            px, py = cx, cy
            pdx = -d0x if side == 0 else d0x
            pdy = -d0y if side == 0 else d0y
            for _ in range(half_steps):
                ix = min(w - 1, max(0, int(px)))
                iy = min(h - 1, max(0, int(py)))
                vx, vy = float(tx[iy, ix]), float(ty[iy, ix])
                if vx * pdx + vy * pdy < 0.0:
                    vx, vy = -vx, -vy
                dx = 0.75 * vx + 0.25 * pdx
                dy = 0.75 * vy + 0.25 * pdy
                dl = math.hypot(dx, dy)
                if dl < 1e-5:
                    break
                nx = px + step_len * (dx / dl)
                ny = py + step_len * (dy / dl)
                if nx < 0.5 or ny < 0.5 or nx >= w - 0.5 or ny >= h - 0.5:
                    break
                iny, inx = int(ny), int(nx)
                if not mask_bool[iny, inx] or float(dist_inside[iny, inx]) < min_clear:
                    break
                cur_c = bilat_rgb[iny, inx].astype(np.float32)
                if float(((cur_c - seed_c) ** 2).mean()) > max_jump_sq:
                    break
                arms[side].append((nx, ny))
                px, py = nx, ny
                pdx, pdy = dx / dl, dy / dl
        raw = list(reversed(arms[0])) + [(cx, cy)] + arms[1]
        if len(raw) < 3:
            return None
        arr = np.asarray(raw, dtype=np.float32)
        t_src = np.linspace(0.0, 1.0, len(arr))
        t_dst = np.linspace(0.0, 1.0, n_ctrl)
        interp_x = np.interp(t_dst, t_src, arr[:, 0])
        interp_y = np.interp(t_dst, t_src, arr[:, 1])
        return np.stack([interp_x, interp_y], axis=-1).astype(np.float32)

    taper_curve = np.sin(np.linspace(0.14, math.pi - 0.14, n_ctrl)).astype(np.float32)
    taper_curve = np.clip(0.28 + 0.72 * taper_curve, 0.22, 1.0)

    # 1) Multiply shadow glazes (~12% of budget)
    flat_lum = 0.299 * flat_rgb_u8[:, :, 0] + 0.587 * flat_rgb_u8[:, :, 1] + 0.114 * flat_rgb_u8[:, :, 2]
    glaze_w = float(np.clip(min(h, w) / 11.0, 8.0, 34.0))
    glaze_clear = max(1.2, glaze_w * 0.50)
    glaze_edge_rad = max(3, int(round(glaze_w * 0.55)))
    glaze_near_edge = cv2.dilate((grad_mag >= 14.0).astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (glaze_edge_rad * 2 + 1, glaze_edge_rad * 2 + 1))) > 0
    shadow_map = np.maximum(0.0, flat_lum - gray) * ((dist_inside >= glaze_clear) & (~glaze_near_edge)).astype(np.float32)
    n_glaze = max(4, int(max_strokes * 0.12))
    supp_shadow = shadow_map.copy()
    for _ in range(n_glaze * 2):
        if len([m for m in modes if m == 'multiply']) >= n_glaze:
            break
        idx = int(np.argmax(supp_shadow))
        if float(supp_shadow.flat[idx]) < 9.0:
            break
        sy, sx = divmod(idx, w)
        cv2.circle(supp_shadow, (sx, sy), int(max(8, glaze_w * 0.65)), 0.0, -1)
        poly = trace_poly(sx, sy, max(2.0, glaze_w * 0.35), 7, glaze_clear, max_jump_sq=1800.0)
        if poly is None:
            continue
        base_c = np.maximum(16.0, flat_rgb_u8[sy, sx].astype(np.float32))
        tgt_c = bilat_rgb[sy, sx].astype(np.float32)
        mult_c = np.clip(tgt_c / base_c, 0.20, 0.96)
        pts_list.append(poly)
        w_list.append(glaze_w * taper_curve)
        rgb_list.append(mult_c)
        op_list.append(0.45)
        soft_list.append(max(2.2, glaze_w * 0.28))
        modes.append('multiply')
        stages.append('artist.form_glaze')

    # 2) Macro soft atmospheric washes (~18% of budget — melts smooth gradient bands cleanly!)
    wash_w = float(np.clip(min(h, w) / 8.5, 14.0, 42.0))
    wash_clear = max(1.2, wash_w * 0.50)
    wash_edge_rad = max(3, int(round(wash_w * 0.55)))
    wash_near_edge = cv2.dilate((grad_mag >= 14.0).astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (wash_edge_rad * 2 + 1, wash_edge_rad * 2 + 1))) > 0
    wash_map = smooth_diff_sq * ((~wash_near_edge) & (dist_inside >= wash_clear)).astype(np.float32)
    n_wash = max(6, int(max_strokes * 0.18))
    supp_wash = wash_map.copy()
    added_wash = 0
    for _ in range(n_wash * 2):
        if added_wash >= n_wash:
            break
        idx = int(np.argmax(supp_wash))
        if float(supp_wash.flat[idx]) < 14.0:
            break
        sy, sx = divmod(idx, w)
        cv2.circle(supp_wash, (sx, sy), int(max(7, wash_w * 0.55)), 0.0, -1)
        poly = trace_poly(sx, sy, max(2.2, wash_w * 0.35), 8, wash_clear, max_jump_sq=2200.0)
        if poly is None:
            continue
        pts_list.append(poly)
        w_list.append(wash_w * taper_curve)
        rgb_list.append(bilat_rgb[sy, sx].astype(np.float32) / 255.0)
        op_list.append(0.62)
        soft_list.append(max(2.5, wash_w * 0.30))
        modes.append('normal')
        stages.append('artist.structure')
        added_wash += 1

    # 3) Medium & fine ETF structure strokes (~45% of budget)
    n_struct = max(14, int(max_strokes * 0.45))
    supp_err = score_map.copy()
    added_struct = 0
    for i in range(n_struct * 2):
        if added_struct >= n_struct:
            break
        idx = int(np.argmax(supp_err))
        if float(supp_err.flat[idx]) < 18.0:
            break
        sy, sx = divmod(idx, w)
        coarse = added_struct < (n_struct // 3)
        width_base = 5.4 if coarse else 2.3
        s_clear = min(4.0, width_base * 0.48)
        rad_supp = 7 if coarse else 3
        cv2.circle(supp_err, (sx, sy), rad_supp, 0.0, -1)
        if float(dist_inside[sy, sx]) < s_clear:
            continue
        poly = trace_poly(sx, sy, max(1.0, width_base * 0.5), 6, s_clear, max_jump_sq=720.0)
        if poly is None:
            continue
        pts_list.append(poly)
        w_list.append(width_base * taper_curve)
        rgb_list.append(rgb_u8[sy, sx].astype(np.float32) / 255.0)
        op_list.append(0.84 if coarse else 0.92)
        soft_list.append(1.25 if coarse else 0.75)
        modes.append('normal')
        stages.append('artist.structure')
        added_struct += 1

    # 4) Hero ink dark ridges traced along XDoG peaks (~16% of budget)
    n_ink = max(6, int(max_strokes * 0.16))
    ink_clear = 1.2
    supp_ink = xdog_dark * roi_weights * (dist_inside >= ink_clear).astype(np.float32)
    added_ink = 0
    for _ in range(n_ink * 2):
        if added_ink >= n_ink:
            break
        idx = int(np.argmax(supp_ink))
        if float(supp_ink.flat[idx]) < 11.0:
            break
        sy, sx = divmod(idx, w)
        cv2.circle(supp_ink, (sx, sy), 5, 0.0, -1)
        poly = trace_poly(sx, sy, 1.35, 7, ink_clear, max_jump_sq=600.0)
        if poly is None:
            continue
        # Estimate local dark stroke diameter perpendicular to ETF
        nx_p, ny_p = -float(ty[sy, sx]), float(tx[sy, sx])
        diam = 1.4
        for step_r in range(1, 5):
            for sgn in (-1, 1):
                ry_i = min(h - 1, max(0, int(round(sy + sgn * step_r * ny_p))))
                rx_i = min(w - 1, max(0, int(round(sx + sgn * step_r * nx_p))))
                if xdog_dark[ry_i, rx_i] > 4.0:
                    diam = max(diam, step_r * 1.45)
        pts_list.append(poly)
        w_list.append(min(5.0, diam) * taper_curve)
        rgb_list.append(rgb_u8[sy, sx].astype(np.float32) / 255.0)
        op_list.append(0.94)
        soft_list.append(0.65)
        modes.append('normal')
        stages.append('artist.hero_ink')
        added_ink += 1

    # 5) Screen specular highlights (~9% of budget)
    hi_clear = 1.5
    hi_map = np.maximum(0.0, gray - flat_lum) * roi_weights * (dist_inside >= hi_clear).astype(np.float32)
    n_hi = max(3, max_strokes - len(pts_list))
    added_hi = 0
    for _ in range(n_hi * 2):
        if added_hi >= n_hi:
            break
        idx = int(np.argmax(hi_map))
        if float(hi_map.flat[idx]) < 16.0:
            break
        sy, sx = divmod(idx, w)
        cv2.circle(hi_map, (sx, sy), 6, 0.0, -1)
        poly = trace_poly(sx, sy, 1.25, 5, hi_clear, max_jump_sq=650.0)
        if poly is None:
            continue
        cb = flat_rgb_u8[sy, sx].astype(np.float32) / 255.0
        tg = rgb_u8[sy, sx].astype(np.float32) / 255.0
        sc = np.clip((tg - cb) / np.maximum(0.12, 1.0 - cb), 0.08, 1.0)
        pts_list.append(poly)
        w_list.append(2.0 * taper_curve)
        rgb_list.append(sc)
        op_list.append(0.68)
        soft_list.append(0.75)
        modes.append('screen')
        stages.append('artist.highlights')
        added_hi += 1

    if not pts_list:
        return (
            np.zeros((0, n_ctrl, 2), np.float32),
            np.zeros((0, n_ctrl), np.float32),
            np.zeros((0, 3), np.float32),
            np.zeros((0,), np.float32),
            np.zeros((0,), np.float32),
            [],
            [],
        )
    return (
        np.stack(pts_list, axis=0),
        np.stack(w_list, axis=0),
        np.stack(rgb_list, axis=0),
        np.asarray(op_list, dtype=np.float32),
        np.asarray(soft_list, dtype=np.float32),
        modes,
        stages,
    )


def _rasterize_tapered_capsule_patch(
    bh: int,
    bw: int,
    bx0: int,
    by0: int,
    chain: list[tuple[float, float]],
    pressures: list[float],
    width: float,
    use_soft: bool,
    hardness: float,
) -> np.ndarray:
    """Analytical subpixel capsule coverage matching C++ CoverageRasterizer (engine2) & FineLineCoverage (engine19)."""
    py = (np.arange(bh, dtype=np.float32) + float(by0) + 0.5)[:, None]
    px = (np.arange(bw, dtype=np.float32) + float(bx0) + 0.5)[None, :]
    cov = np.zeros((bh, bw), dtype=np.float32)
    half_w = 0.5 * float(width)
    for k in range(1, len(chain)):
        ax, ay = chain[k - 1]
        bx, by = chain[k]
        pa, pb = pressures[k - 1], pressures[k]
        vx, vy = bx - ax, by - ay
        seg_l2 = vx * vx + vy * vy
        if seg_l2 <= 1e-8:
            t = np.zeros((bh, bw), dtype=np.float32)
            dx = px - ax
            dy = py - ay
        else:
            t = np.clip(((px - ax) * vx + (py - ay) * vy) / seg_l2, 0.0, 1.0)
            dx = px - (ax + t * vx)
            dy = py - (ay + t * vy)
        dist = np.hypot(dx, dy)
        rad = np.maximum(0.05, half_w * (pa + t * (pb - pa)))
        if use_soft:
            inner = np.maximum(0.0, rad * float(hardness) - 0.5)
            outer = rad + 0.5
            seg_cov = np.clip((outer - dist) / np.maximum(1e-4, outer - inner), 0.0, 1.0)
        else:
            seg_cov = np.clip(rad + 0.5 - dist, 0.0, 1.0)
        cov = np.maximum(cov, seg_cov)
    return cov


def _run_verified_residual_alignment(
    rgb_u8: np.ndarray,
    cpp_rgb_u8: np.ndarray,
    roi_weights_np: np.ndarray,
    tx_np: np.ndarray,
    ty_np: np.ndarray,
    plan_objects: list[dict],
    obj_targets_and_masks: list[tuple[np.ndarray, np.ndarray, np.ndarray, bool]],
    is_super_res: bool = False,
) -> bool:
    """Solve mask-safe, occlusion-aware Linear-sRGB ETF strokes against live C++ residual canvas."""
    h, w = rgb_u8.shape[:2]
    cpp_lin = _srgb_u8_to_lin_np(cpp_rgb_u8)
    cpp_srgb_f = cpp_rgb_u8.astype(np.float32)
    changed = False

    for p_obj, (target_rgb_u8, m_bool, vis_bool, is_full_bg) in zip(plan_objects, obj_targets_and_masks):
        if not np.any(m_bool) or not np.any(vis_bool):
            continue
        target_lin = _srgb_u8_to_lin_np(target_rgb_u8)
        target_f = target_rgb_u8.astype(np.float32)
        bilat_f = cv2.bilateralFilter(target_rgb_u8, d=5, sigmaColor=18, sigmaSpace=5).astype(np.float32)
        bilat_lin = _srgb_u8_to_lin_np(np.clip(bilat_f, 0, 255).astype(np.uint8))

        if is_full_bg:
            dist_in = np.full((h, w), 64.0, dtype=np.float32)
        else:
            dist_in = cv2.distanceTransform(m_bool.astype(np.uint8), cv2.DIST_L2, 3)
        occ_depth = (
            np.zeros((h, w), dtype=np.float32)
            if np.array_equal(m_bool, vis_bool)
            else cv2.distanceTransform((~vis_bool).astype(np.uint8), cv2.DIST_L2, 3)
        )

        gray = cv2.cvtColor(target_rgb_u8, cv2.COLOR_RGB2GRAY).astype(np.float32)
        gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3) * 0.25
        gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3) * 0.25
        grad_mag = np.hypot(gx, gy)

        ref_strokes: list[dict] = []
        # 4 multi-scale passes: soft atmospheric wash -> mid form -> crisp structure -> focal fine detail
        wash_scale = float(np.clip(min(h, w) / 9.5, 16.0, 48.0))
        passes = [
            (wash_scale, 12, 85, 6.0, True, 0.16, 0.58, 120.0),
            (5.4, 5, 110, 12.0, True, 0.40, 0.82, 55.0),
            (2.6, 3, 240, 11.0, False, 1.0, 0.90, 20.0),
            (1.35, 2, 360, 8.0, False, 1.0, 0.94, 8.0),
        ]

        for scale_w, r_supp, n_budget, thresh, use_soft, hardness, opacity, min_gain in passes:
            if is_super_res and scale_w < 2.0:
                continue
            req_clear = 0.0 if is_full_bg else max(1.1, scale_w * 0.50 + 0.35)
            max_tuck = max(2.5, scale_w * 0.65)
            valid_seed_mask = vis_bool & (dist_in >= req_clear)
            if use_soft and scale_w >= 10.0:
                edge_r = max(3, int(round(scale_w * 0.55)))
                near_step_edge = cv2.dilate(
                    (grad_mag >= 14.0).astype(np.uint8),
                    cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (edge_r * 2 + 1, edge_r * 2 + 1)),
                ) > 0
                valid_seed_mask = valid_seed_mask & (~near_step_edge)
            if not np.any(valid_seed_mask):
                continue

            ref_img = bilat_f if (use_soft and scale_w >= 14.0) else target_f
            err_map = ((ref_img - cpp_srgb_f) ** 2).mean(axis=2)
            w_mult = 1.0 if (use_soft and scale_w >= 14.0) else roi_weights_np
            supp = err_map * w_mult * valid_seed_mask.astype(np.float32)

            step_len = max(0.75, scale_w * (0.35 if use_soft else 0.48))
            half_steps = 12 if scale_w >= 14.0 else (9 if scale_w >= 5.0 else (7 if scale_w >= 2.2 else 5))
            max_jump = 1600.0 if scale_w >= 14.0 else (680.0 if scale_w >= 5.0 else 440.0)

            accepted_in_pass = 0
            for _ in range(n_budget * 2):
                if accepted_in_pass >= n_budget:
                    break
                idx = int(np.argmax(supp))
                if float(supp.flat[idx]) < thresh:
                    break
                sy, sx = divmod(idx, w)
                cv2.circle(supp, (sx, sy), r_supp, 0.0, -1)

                # Re-verify live error at seed (may already be healed by previous wide stroke!)
                live_e = float(((ref_img[sy, sx] - cpp_srgb_f[sy, sx]) ** 2).mean())
                if live_e < thresh * 0.45:
                    continue

                # Trace RK2 ETF streamline strictly respecting dist_in >= req_clear
                cx, cy = float(sx) + 0.5, float(sy) + 0.5
                d0x, d0y = float(tx_np[sy, sx]), float(ty_np[sy, sx])
                seed_c = bilat_f[sy, sx]
                arms: list[list[tuple[float, float]]] = [[], []]
                for side in range(2):
                    px, py = cx, cy
                    pdx = -d0x if side == 0 else d0x
                    pdy = -d0y if side == 0 else d0y
                    for _ in range(half_steps):
                        ix = min(w - 1, max(0, int(px)))
                        iy = min(h - 1, max(0, int(py)))
                        vx, vy = float(tx_np[iy, ix]), float(ty_np[iy, ix])
                        if vx * pdx + vy * pdy < 0.0:
                            vx, vy = -vx, -vy
                        dx = 0.75 * vx + 0.25 * pdx
                        dy = 0.75 * vy + 0.25 * pdy
                        dl = math.hypot(dx, dy)
                        if dl < 1e-5:
                            break
                        nx = px + step_len * (dx / dl)
                        ny = py + step_len * (dy / dl)
                        if nx < 0.5 or ny < 0.5 or nx >= w - 0.5 or ny >= h - 0.5:
                            break
                        iny, inx = int(ny), int(nx)
                        if (
                            not m_bool[iny, inx]
                            or float(dist_in[iny, inx]) < req_clear
                            or float(occ_depth[iny, inx]) > max_tuck
                        ):
                            break
                        if float(((bilat_f[iny, inx] - seed_c) ** 2).mean()) > max_jump:
                            break
                        arms[side].append((nx, ny))
                        px, py = nx, ny
                        pdx, pdy = dx / dl, dy / dl

                chain = list(reversed(arms[0])) + [(cx, cy)] + arms[1]
                if len(chain) == 1 and (not is_super_res) and scale_w <= 1.8:
                    ax, ay = cx - 0.42 * d0x, cy - 0.42 * d0y
                    bx, by = cx + 0.42 * d0x, cy + 0.42 * d0y
                    if (
                        0.5 <= ax < w - 0.5
                        and 0.5 <= ay < h - 0.5
                        and 0.5 <= bx < w - 0.5
                        and 0.5 <= by < h - 0.5
                        and m_bool[int(ay), int(ax)]
                        and m_bool[int(by), int(bx)]
                        and float(dist_in[int(ay), int(ax)]) >= req_clear
                        and float(dist_in[int(by), int(bx)]) >= req_clear
                    ):
                        chain = [(ax, ay), (bx, by)]
                min_pts = 2 if ((not is_super_res) and scale_w <= 1.8) else 3
                if len(chain) < min_pts:
                    continue
                arc_len = sum(
                    math.hypot(chain[k][0] - chain[k - 1][0], chain[k][1] - chain[k - 1][1])
                    for k in range(1, len(chain))
                )
                min_arc = 0.75 if ((not is_super_res) and scale_w <= 1.8) else max(2.0, scale_w * 0.90)
                if arc_len < min_arc:
                    continue

                n_pts = len(chain)
                pressures = [
                    float(np.clip(0.45 + 0.55 * math.sin((float(pi) / float(n_pts - 1)) * math.pi), 0.35, 1.0))
                    for pi in range(n_pts)
                ]

                # Rasterize exact analytical tapered capsule footprint in ROI bounding box!
                xs_c = [p[0] for p in chain]
                ys_c = [p[1] for p in chain]
                pad = int(math.ceil(scale_w * 0.65 + 2.0))
                bx0 = max(0, int(math.floor(min(xs_c))) - pad)
                by0 = max(0, int(math.floor(min(ys_c))) - pad)
                bx1 = min(w, int(math.ceil(max(xs_c))) + pad + 1)
                by1 = min(h, int(math.ceil(max(ys_c))) + pad + 1)
                if bx0 >= bx1 or by0 >= by1:
                    continue

                bw, bh = bx1 - bx0, by1 - by0
                mask_patch = _rasterize_tapered_capsule_patch(
                    bh, bw, bx0, by0, chain, pressures, scale_w, use_soft, hardness
                )

                roi_m = vis_bool[by0:by1, bx0:bx1]
                alpha_patch = mask_patch * float(opacity) * roi_m.astype(np.float32)
                denom = float((alpha_patch * alpha_patch).sum())
                if denom < 1e-4:
                    continue

                cur_lin_patch = cpp_lin[by0:by1, bx0:bx1]
                tgt_lin_patch = (bilat_lin if (use_soft and scale_w >= 14.0) else target_lin)[by0:by1, bx0:bx1]
                a3 = alpha_patch[:, :, None]
                opt_lin = ((a3 * (tgt_lin_patch - (1.0 - a3) * cur_lin_patch)).sum(axis=(0, 1))) / denom
                opt_lin = np.clip(opt_lin, 0.0, 1.0)
                opt_srgb01 = _lin_to_srgb_01_np(opt_lin)

                new_lin_patch = cur_lin_patch * (1.0 - a3) + opt_lin[None, None, :] * a3
                new_srgb_patch = _lin_to_srgb_01_np(new_lin_patch) * 255.0
                old_srgb_patch = cpp_srgb_f[by0:by1, bx0:bx1]
                tgt_srgb_patch = ref_img[by0:by1, bx0:bx1]

                m_f = roi_m[:, :, None].astype(np.float32)
                old_sq = float((((old_srgb_patch - tgt_srgb_patch) ** 2) * m_f).sum()) / 3.0
                new_sq = float((((new_srgb_patch - tgt_srgb_patch) ** 2) * m_f).sum()) / 3.0
                if old_sq - new_sq < min_gain:
                    continue

                # Commit improvement to live canvas!
                cpp_lin[by0:by1, bx0:bx1] = new_lin_patch
                cpp_srgb_f[by0:by1, bx0:bx1] = new_srgb_patch

                pts_json = [
                    [round(chain[pi][0], 3), round(chain[pi][1], 3), round(pressures[pi], 3)]
                    for pi in range(n_pts)
                ]

                c_rgba = [round(float(opt_srgb01[c]), 4) for c in range(3)] + [1.0]
                if use_soft:
                    ref_strokes.append({
                        'engine': 2,
                        'tool': 'marker',
                        'width': round(scale_w, 3),
                        'opacity': round(opacity, 4),
                        'hardness': round(hardness, 3),
                        'color': c_rgba,
                        'linearOptical': True,
                        'points': pts_json,
                    })
                else:
                    chain_lums = [float(gray[min(h - 1, int(py)), min(w - 1, int(px))]) for px, py in chain]
                    use_dry_tex = (
                        (not is_super_res)
                        and (2.4 <= scale_w <= 3.5)
                        and (grad_mag[sy, sx] > 16.0)
                        and (float(np.std(chain_lums)) > 11.0)
                        and (len(ref_strokes) % 5 == 0)
                    )
                    ref_strokes.append({
                        'engine': 12 if use_dry_tex else 19,
                        'tool': 'dry-ink' if use_dry_tex else 'smooth-ink-aa',
                        'width': round(scale_w, 3),
                        'opacity': round(opacity, 4),
                        'color': c_rgba,
                        'seed': int(31337 + len(ref_strokes) * 53),
                        'physicalDryInk': bool(use_dry_tex),
                        'linearOptical': True,
                        'points': pts_json,
                    })
                accepted_in_pass += 1

        if ref_strokes:
            # Append as top normal-blend Linear-sRGB polish layer per object so probe_native's
            # composite (including Multiply glazes & Screen highlights) is the exact base!
            p_obj['layers'].append(
                {
                    'name': 'Detail & Edge Polish',
                    'stage': 'artist.structure',
                    'blend': 'normal',
                    'opacity': 1.0,
                    'visible': True,
                    'strokes': ref_strokes,
                }
            )
            changed = True

    return changed


def optimize_manifest_to_native(
    manifest_path: Path,
    engine_path: Path,
    output_dir: Path,
    config: DifferentiableArtistConfig | None = None,
    allow_fixture: bool = False,
) -> dict:
    """Run differentiable perceptual stroke optimization and compile via C++ DrawboradHeadless."""
    cfg = config or DifferentiableArtistConfig()
    manifest_file = Path(manifest_path).resolve()
    doc = load_manifest(manifest_file.parent)
    if doc.manifest['backend'] == 'fixture-no-ai' and not allow_fixture:
        raise ValueError('Synthetic fixture requires explicit allow_fixture=True')

    with Image.open(doc.image_path) as im:
        from PIL import ImageOps
        rgb_u8 = np.asarray(ImageOps.exif_transpose(im).convert('RGB'), dtype=np.uint8)

    h, w = doc.height, doc.width
    masks = [(doc.read_mask(layer).astype(np.uint8) * 255) for layer in doc.layers]
    names = [layer.name for layer in doc.layers]
    is_bg = [layer.is_background for layer in doc.layers]

    roi_weights_np = compute_semantic_roi_weights(rgb_u8, masks, names, is_bg)
    tx_np, ty_np, _ = compute_lab_etf(rgb_u8, iterations=3)

    if torch is not None:
        device = torch.device('cpu')
        lpips_fn, dists_fn = _build_perceptual_losses(device) if cfg.use_perceptual_loss else (None, None)
        etf_grid = torch.from_numpy(np.stack([tx_np, ty_np], axis=0)).unsqueeze(0).to(device)  # [1, 2, H, W]
    else:
        device = None
        lpips_fn, dists_fn = None, None
        etf_grid = None
    is_super_res = cfg.target_size is not None and (cfg.target_size[0] != w or cfg.target_size[1] != h)

    plan_objects: list[dict] = []
    obj_targets_and_masks: list[tuple[np.ndarray, np.ndarray, np.ndarray, bool]] = []
    for obj_idx, (layer_meta, raw_mask_u8) in enumerate(zip(doc.layers, masks)):
        amodal_active = cfg.amodal_background and obj_idx == 0 and layer_meta.is_background and len(doc.layers) >= 2
        if amodal_active:
            obj_target_rgb = _inpaint_amodal_background(rgb_u8, raw_mask_u8)
            obj_mask_bool = np.ones((h, w), dtype=bool)
            is_full_bg = True
        else:
            obj_target_rgb = rgb_u8
            obj_mask_bool = raw_mask_u8 > 0
            is_full_bg = bool(np.all(obj_mask_bool))

        higher_union = np.zeros((h, w), dtype=bool)
        for higher_m in masks[obj_idx + 1 :]:
            higher_union |= (higher_m > 0)
        if np.any(higher_union):
            occ_dilated = cv2.dilate(higher_union.astype(np.uint8), np.ones((3, 3), dtype=np.uint8), iterations=1) > 0
            vis_mask_bool = obj_mask_bool & (~occ_dilated)
            if not np.any(vis_mask_bool):
                vis_mask_bool = obj_mask_bool & (~higher_union)
        else:
            vis_mask_bool = obj_mask_bool

        ys, xs = np.nonzero(obj_mask_bool)
        if len(ys) == 0:
            continue
        ry0 = max(0, int(ys.min()) - 6)
        ry1 = min(h, int(ys.max()) + 7)
        rx0 = max(0, int(xs.min()) - 6)
        rx1 = min(w, int(xs.max()) + 7)

        roi_rgb = obj_target_rgb[ry0:ry1, rx0:rx1]
        roi_mask = obj_mask_bool[ry0:ry1, rx0:rx1]
        roi_w = roi_weights_np[ry0:ry1, rx0:rx1]
        roi_tx = tx_np[ry0:ry1, rx0:rx1]
        roi_ty = ty_np[ry0:ry1, rx0:rx1]

        flat_strokes, flat_rgba = _extract_flat_polygons_and_canvas(
            obj_target_rgb,
            obj_mask_bool,
            roi_weights=roi_weights_np,
            max_clusters=28 if is_super_res else 42,
            use_polygons_for_silhouette=is_super_res,
        )
        roi_flat_rgb = flat_rgba[ry0:ry1, rx0:rx1, :3]

        rh_full, rw_full = roi_rgb.shape[:2]
        max_opt_dim = 448
        if max(rh_full, rw_full) > max_opt_dim:
            opt_scale = float(max_opt_dim) / float(max(rh_full, rw_full))
            oh = max(16, int(round(rh_full * opt_scale)))
            ow = max(16, int(round(rw_full * opt_scale)))
            opt_rgb = cv2.resize(roi_rgb, (ow, oh), interpolation=cv2.INTER_AREA)
            opt_flat_rgb = cv2.resize(roi_flat_rgb, (ow, oh), interpolation=cv2.INTER_AREA)
            opt_mask = cv2.resize(roi_mask.astype(np.uint8), (ow, oh), interpolation=cv2.INTER_NEAREST) > 0
            if not np.any(opt_mask):
                opt_mask = cv2.resize(roi_mask.astype(np.float32), (ow, oh), interpolation=cv2.INTER_LINEAR) > 0.05
            opt_w_map = cv2.resize(roi_w, (ow, oh), interpolation=cv2.INTER_LINEAR)
            opt_tx = cv2.resize(roi_tx, (ow, oh), interpolation=cv2.INTER_LINEAR)
            opt_ty = cv2.resize(roi_ty, (ow, oh), interpolation=cv2.INTER_LINEAR)
            tnorm = np.maximum(1e-6, np.hypot(opt_tx, opt_ty))
            opt_tx = (opt_tx / tnorm).astype(np.float32)
            opt_ty = (opt_ty / tnorm).astype(np.float32)
            sx_back = float(rw_full) / float(ow)
            sy_back = float(rh_full) / float(oh)
            sw_back = math.sqrt(sx_back * sy_back)
            roi_etf_t = torch.from_numpy(np.stack([opt_tx, opt_ty], axis=0)).unsqueeze(0).to(device) if torch is not None else None
        else:
            opt_rgb = roi_rgb
            opt_flat_rgb = roi_flat_rgb
            opt_mask = roi_mask
            opt_w_map = roi_w
            opt_tx = roi_tx
            opt_ty = roi_ty
            sx_back = 1.0
            sy_back = 1.0
            sw_back = 1.0
            roi_etf_t = etf_grid[:, :, ry0:ry1, rx0:rx1] if etf_grid is not None else None

        if is_full_bg:
            opt_dist = np.full(opt_mask.shape, 64.0, dtype=np.float32)
        else:
            opt_dist = cv2.distanceTransform(opt_mask.astype(np.uint8), cv2.DIST_L2, 3)

        pts_np, widths_np, srgb_np, op_np, soft_np, modes, stages = _seed_etf_strokes(
            opt_rgb, opt_flat_rgb, opt_mask, opt_dist, opt_w_map, opt_tx, opt_ty, cfg.max_strokes_per_object
        )

        stage_buckets: dict[str, list[dict]] = {
            'artist.flats': flat_strokes,
            'artist.form_glaze': [],
            'artist.structure': [],
            'artist.hero_ink': [],
            'artist.highlights': [],
        }

        if torch is not None and len(pts_np) > 0 and cfg.steps > 0:
            field = DifferentiableStrokeField(
                torch.from_numpy(pts_np).to(device),
                torch.from_numpy(widths_np).to(device),
                torch.from_numpy(srgb_np).to(device),
                torch.from_numpy(op_np).to(device),
                blend_modes=modes,
                softness=torch.from_numpy(soft_np).to(device),
            ).to(device)

            target_srgb_t = torch.from_numpy(opt_rgb.astype(np.float32) / 255.0).to(device)
            target_lin_t = srgb_to_linear_torch(target_srgb_t)
            base_srgb_t = torch.from_numpy(opt_flat_rgb.astype(np.float32) / 255.0).to(device)
            base_lin_t = srgb_to_linear_torch(base_srgb_t)
            weight_t = torch.from_numpy(opt_w_map * opt_mask.astype(np.float32)).unsqueeze(-1).to(device)
            mask_sum = float(weight_t.sum().item()) * 3.0 + 1e-5
            rh_i, rw_i = opt_rgb.shape[:2]
            m_chw = torch.from_numpy(opt_mask.astype(np.float32)).view(1, 1, rh_i, rw_i).to(device)
            init_pts_t = torch.from_numpy(pts_np).to(device)
            init_w_max_t = torch.from_numpy(widths_np.max(axis=1) * 1.65).to(device).unsqueeze(1)

            opt = torch.optim.Adam([
                {'params': [field.points], 'lr': cfg.lr_points},
                {'params': [field.log_widths], 'lr': cfg.lr_widths},
                {'params': [field.rgb_logits], 'lr': cfg.lr_color},
                {'params': [field.opacity_logits], 'lr': cfg.lr_opacity},
            ])

            for step in range(cfg.steps):
                opt.zero_grad()
                pred_lin = field(base_lin_t, tile_size=64)
                pred_srgb = linear_to_srgb_torch(pred_lin)

                l1_roi = (torch.abs(pred_lin - target_lin_t) * weight_t).sum() / mask_sum

                perc_loss = torch.tensor(0.0, device=device)
                if (lpips_fn is not None or dists_fn is not None) and (step % 3 == 0) and min(rh_i, rw_i) >= 48:
                    p_chw = pred_srgb.permute(2, 0, 1).unsqueeze(0)
                    t_chw = target_srgb_t.permute(2, 0, 1).unsqueeze(0)
                    p_masked = p_chw * m_chw + t_chw * (1.0 - m_chw)
                    if max(rh_i, rw_i) > 256:
                        scale = 256.0 / float(max(rh_i, rw_i))
                        nh = max(64, int(round(rh_i * scale)))
                        nw = max(64, int(round(rw_i * scale)))
                        p_eval = F.interpolate(p_masked, size=(nh, nw), mode='bilinear', align_corners=False)
                        t_eval = F.interpolate(t_chw, size=(nh, nw), mode='bilinear', align_corners=False)
                    elif min(rh_i, rw_i) < 64:
                        p_eval = F.interpolate(p_masked, size=(64, 64), mode='bilinear', align_corners=False)
                        t_eval = F.interpolate(t_chw, size=(64, 64), mode='bilinear', align_corners=False)
                    else:
                        p_eval, t_eval = p_masked, t_chw
                    if lpips_fn is not None:
                        perc_loss = perc_loss + 0.14 * lpips_fn(p_eval * 2.0 - 1.0, t_eval * 2.0 - 1.0).mean()
                    if dists_fn is not None:
                        perc_loss = perc_loss + 0.12 * dists_fn(p_eval, t_eval).mean()

                pts_cur, _, _, op_cur = field.current_parameters()
                seg_vec = pts_cur[:, 1:, :] - pts_cur[:, :-1, :]
                seg_len = torch.sqrt((seg_vec * seg_vec).sum(dim=-1) + 1e-6)
                seg_dir = seg_vec / seg_len.unsqueeze(-1)
                mid_pts = 0.5 * (pts_cur[:, 1:, :] + pts_cur[:, :-1, :])
                gx_n = (mid_pts[:, :, 0] / max(1.0, float(rw_i - 1))) * 2.0 - 1.0
                gy_n = (mid_pts[:, :, 1] / max(1.0, float(rh_i - 1))) * 2.0 - 1.0
                sample_grid = torch.stack([gx_n, gy_n], dim=-1).unsqueeze(0)
                sampled_etf = F.grid_sample(roi_etf_t, sample_grid, mode='bilinear', padding_mode='border', align_corners=True)
                etf_dirs = sampled_etf.squeeze(0).permute(1, 2, 0)
                cos_align = torch.abs((seg_dir * etf_dirs).sum(dim=-1))
                etf_loss = 0.015 * (1.0 - cos_align).mean()

                curv = pts_cur[:, 2:, :] - 2.0 * pts_cur[:, 1:-1, :] + pts_cur[:, :-2, :]
                smooth_loss = 0.003 * (curv * curv).sum(dim=-1).mean()
                sparse_loss = 0.003 * op_cur.mean()

                loss = l1_roi + perc_loss + etf_loss + smooth_loss + sparse_loss
                loss.backward()
                opt.step()

                with torch.no_grad():
                    field.points.copy_(torch.clamp(field.points, init_pts_t - 2.0, init_pts_t + 2.0))
                    field.points[:, :, 0].clamp_(0.5, float(rw_i) - 0.5)
                    field.points[:, :, 1].clamp_(0.5, float(rh_i) - 0.5)
                    cur_w = torch.exp(field.log_widths)
                    field.log_widths.copy_(torch.log(torch.clamp(cur_w, min=0.35 * torch.ones_like(cur_w), max=init_w_max_t)))

            with torch.no_grad():
                opt_pts, opt_w, opt_rgb_p, opt_op = field.current_parameters()
                pts_final = opt_pts.cpu().numpy()
                w_final = opt_w.cpu().numpy()
                rgb_final = opt_rgb_p.cpu().numpy()
                op_final = opt_op.cpu().numpy()
        else:
            pts_final, w_final, rgb_final, op_final = pts_np.copy(), widths_np.copy(), srgb_np, op_np

        if len(pts_final) > 0 and (sx_back != 1.0 or sy_back != 1.0):
            pts_final[:, :, 0] *= sx_back
            pts_final[:, :, 1] *= sy_back
            ink_scale = math.sqrt(sw_back)
            for s_i, st_name in enumerate(stages):
                w_final[s_i] *= (ink_scale if st_name == 'artist.hero_ink' else sw_back)

        full_dist = (
            np.full((h, w), 64.0, dtype=np.float32)
            if is_full_bg
            else cv2.distanceTransform(obj_mask_bool.astype(np.uint8), cv2.DIST_L2, 3)
        )

        for s_i in range(len(pts_final)):
            op_val = float(op_final[s_i])
            if op_val < 0.06:
                continue
            global_pts = pts_final[s_i].copy()
            global_pts[:, 0] = np.clip(global_pts[:, 0] + float(rx0), 0.25, float(w) - 0.25)
            global_pts[:, 1] = np.clip(global_pts[:, 1] + float(ry0), 0.25, float(h) - 0.25)
            max_w = max(0.45, float(w_final[s_i].max()))
            min_clear = 0.0 if is_full_bg else max(1.1, max_w * 0.50 + 0.35)

            valid_pts = []
            for k_i in range(len(global_pts)):
                px, py = float(global_pts[k_i, 0]), float(global_pts[k_i, 1])
                iy, ix = min(h - 1, int(py)), min(w - 1, int(px))
                if obj_mask_bool[iy, ix] and float(full_dist[iy, ix]) >= min_clear:
                    pr = float(np.clip(w_final[s_i, k_i] / max_w, 0.14, 1.0))
                    valid_pts.append([round(px, 3), round(py, 3), round(pr, 3)])
            if len(valid_pts) < 2:
                continue
            arc_len = sum(
                math.hypot(valid_pts[k][0] - valid_pts[k - 1][0], valid_pts[k][1] - valid_pts[k - 1][1])
                for k in range(1, len(valid_pts))
            )
            if arc_len < max(1.5, max_w * 0.65):
                continue

            st_name = stages[s_i]
            is_wash = soft_np[s_i] >= 1.8
            color_rgba = [
                round(float(np.clip(rgb_final[s_i, 0], 0.0, 1.0)), 4),
                round(float(np.clip(rgb_final[s_i, 1], 0.0, 1.0)), 4),
                round(float(np.clip(rgb_final[s_i, 2], 0.0, 1.0)), 4),
                1.0,
            ]
            if st_name == 'artist.form_glaze':
                stroke_json = {
                    'engine': 2,
                    'tool': 'marker',
                    'width': round(max_w, 3),
                    'opacity': round(min(0.75, op_val), 4),
                    'hardness': 0.20,
                    'color': color_rgba,
                    'linearOptical': True,
                    'points': valid_pts,
                }
            elif st_name == 'artist.hero_ink':
                pts_lums = [
                    float(
                        0.299 * obj_target_rgb[min(h - 1, int(p[1])), min(w - 1, int(p[0])), 0]
                        + 0.587 * obj_target_rgb[min(h - 1, int(p[1])), min(w - 1, int(p[0])), 1]
                        + 0.114 * obj_target_rgb[min(h - 1, int(p[1])), min(w - 1, int(p[0])), 2]
                    )
                    for p in valid_pts
                ]
                use_dry = (not is_super_res) and (max_w >= 2.6) and (float(np.std(pts_lums)) > 11.0)
                stroke_json = {
                    'engine': 12 if use_dry else 19,
                    'tool': 'dry-ink' if use_dry else 'smooth-ink-aa',
                    'width': round(max(0.5 if use_dry else 0.35, max_w), 3),
                    'opacity': round(op_val, 4),
                    'color': color_rgba,
                    'seed': int(10007 + obj_idx * 997 + s_i * 37),
                    'physicalDryInk': bool(use_dry),
                    'linearOptical': True,
                    'points': valid_pts,
                }
            else:
                use_soft = st_name == 'artist.structure' and (is_wash or max_w >= 5.0)
                stroke_json = {
                    'engine': 2 if use_soft else 19,
                    'tool': 'marker' if use_soft else 'smooth-ink-aa',
                    'width': round(max_w, 3),
                    'opacity': round(op_val, 4),
                    'hardness': (0.18 if is_wash else 0.36) if use_soft else 1.0,
                    'color': color_rgba,
                    'linearOptical': True,
                    'points': valid_pts,
                }
            stage_buckets[st_name].append(stroke_json)

        layer_defs = [
            ('Flats · Silhouette', 'artist.flats', 'normal', 1.0),
            ('Form & Shadow Glaze', 'artist.form_glaze', 'multiply', 0.85),
            ('Surface Structure', 'artist.structure', 'normal', 1.0),
            ('Hero Lineart & Dry Ink', 'artist.hero_ink', 'normal', 1.0),
            ('Highlights & Accents', 'artist.highlights', 'screen', 0.90),
        ]
        obj_layers = []
        for l_title, l_stage, l_blend, l_op in layer_defs:
            if stage_buckets[l_stage]:
                obj_layers.append({
                    'name': l_title,
                    'stage': l_stage,
                    'blend': l_blend,
                    'opacity': l_op,
                    'visible': True,
                    'strokes': stage_buckets[l_stage],
                })
        if obj_layers:
            plan_objects.append({'name': layer_meta.name, 'visible': True, 'layers': obj_layers})
            obj_targets_and_masks.append((obj_target_rgb, obj_mask_bool, vis_mask_bool, is_full_bg))

    plan_payload = {
        'schema': 'drawborad.stroke-plan.v1',
        'planner': 'artist-opt',
        'backend': doc.manifest['backend'],
        'width': w,
        'height': h,
        'dpi': cfg.target_dpi,
        'objects': plan_objects,
    }

    with tempfile.TemporaryDirectory() as tmp_root:
        tmp_dir = Path(tmp_root)
        plan_path = tmp_dir / 'plan_initial.json'
        plan_path.write_text(json.dumps(plan_payload), encoding='utf-8')

        if cfg.cpp_residual_pass and plan_objects:
            probe_out = tmp_dir / 'probe_native'
            probe_cmd = [str(engine_path), 'compile-plan', '--plan', str(plan_path), '--output', str(probe_out)]
            probe_res = subprocess.run(probe_cmd, capture_output=True, text=True, timeout=180)
            if probe_res.returncode == 0 and (probe_out / 'final.png').is_file():
                with Image.open(probe_out / 'final.png') as c_im:
                    cpp_rgb = np.asarray(c_im.convert('RGB'), dtype=np.uint8)
                changed_plan = _run_verified_residual_alignment(
                    rgb_u8,
                    cpp_rgb,
                    roi_weights_np,
                    tx_np,
                    ty_np,
                    plan_objects,
                    obj_targets_and_masks,
                    is_super_res=is_super_res,
                )
                if changed_plan:
                    plan_path = tmp_dir / 'plan_aligned.json'
                    plan_path.write_text(json.dumps(plan_payload), encoding='utf-8')

        compile_cmd = [str(engine_path), 'compile-plan', '--plan', str(plan_path), '--output', str(output_dir)]
        if cfg.target_size is not None:
            compile_cmd += ['--target-size', f'{cfg.target_size[0]}x{cfg.target_size[1]}']
        compile_cmd += ['--target-dpi', str(cfg.target_dpi)]
        final_res = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=240)
        if final_res.returncode != 0:
            raise RuntimeError(f'compile-plan failed ({final_res.returncode}): {final_res.stderr.strip()}')
        (Path(output_dir) / 'stroke_plan.json').write_text(json.dumps(plan_payload), encoding='utf-8')
        return json.loads(final_res.stdout)
