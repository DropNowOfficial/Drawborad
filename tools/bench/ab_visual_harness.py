#!/usr/bin/env python3
"""Automated A/B Perceptual & Visual Evaluation Harness for Drawborad.

Benchmarks legacy `sweep` and `multiscale` planners against `artist` (C++ Artist
Layer Grammar) and `artist-opt` (PyTorch LPIPS/DISTS Differentiable Optimizer +
C++ compile-plan), plus 1024x1536 -> 3300x5100 (300 DPI) native super-resolution
and Amodal occluded background verification.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from skimage import color as skcolor, data as skdata
import torch

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from cloud.planner.optimizer import DifferentiableArtistConfig, optimize_manifest_to_native
from cloud.planner.saliency import compute_lab_etf


def _decode_rle_mask(path: Path) -> np.ndarray:
    obj = json.loads(path.read_text(encoding='utf-8'))
    w, h = int(obj['width']), int(obj['height'])
    val = int(obj['first_value'])
    out = np.empty(w * h, dtype=np.uint8)
    pos = 0
    for run in obj['runs']:
        r = int(run)
        out[pos:pos + r] = val
        pos += r
        val = 0 if val == 255 else 255
    assert pos == w * h
    raw = out.tobytes()
    assert hashlib.sha256(raw).hexdigest() == obj['sha256']
    return out.reshape((h, w))


def _write_manifest_bundle(
    folder: Path,
    rgb_u8: np.ndarray,
    objects: list[tuple[str, np.ndarray, bool]],
    backend: str = 'sam3-image',
    amodal_bg: bool = True,
) -> Path:
    (folder / 'masks').mkdir(parents=True, exist_ok=True)
    h, w = rgb_u8.shape[:2]
    src_path = folder / 'source.png'
    Image.fromarray(rgb_u8, mode='RGB').save(src_path)
    src_sha = hashlib.sha256(src_path.read_bytes()).hexdigest()

    obj_entries = []
    for idx, (name, mask_u8, is_bg) in enumerate(objects):
        raw = mask_u8.astype(np.uint8).tobytes()
        rel = f'masks/layer-{idx:02d}.u8'
        (folder / rel).write_bytes(raw)
        obj_entries.append({
            'name': name,
            'score': 0.96,
            'isBackground': bool(is_bg),
            'pixels': int(np.count_nonzero(mask_u8)),
            'mask': rel,
            'sha256': hashlib.sha256(raw).hexdigest(),
        })

    manifest = {
        'schema': 1,
        'backend': backend,
        'promptOrder': 'back-to-front',
        'amodalBackground': bool(amodal_bg),
        'image': 'source.png',
        'sourceSha256': src_sha,
        'width': w,
        'height': h,
        'objects': obj_entries,
    }
    m_path = folder / 'manifest.json'
    m_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return m_path


def _make_coffee_manifest(folder: Path) -> tuple[Path, np.ndarray, list[tuple[str, tuple[int, int, int, int]]]]:
    rgb = skdata.coffee()
    fixture_dir = ROOT / 'tests/fixtures/coffee-reviewed-scene'
    review = json.loads((fixture_dir / 'review.json').read_text(encoding='utf-8'))
    objects = []
    for idx, reg in enumerate(review['regions']):
        mask = _decode_rle_mask(fixture_dir / f'mask{idx}.json')
        objects.append((reg['name'], mask, idx == 0))
    m_path = _write_manifest_bundle(folder, rgb, objects, backend='sam3-image', amodal_bg=False)
    rois = [
        ('Cup Rim & Crema', (215, 75, 115, 115)),
        ('Handle & Table Shadow', (385, 155, 120, 120)),
    ]
    return m_path, rgb, rois


def _make_chelsea_manifest(folder: Path) -> tuple[Path, np.ndarray, list[tuple[str, tuple[int, int, int, int]]]]:
    rgb = skdata.chelsea()  # 300 x 451 -> h=300, w=451
    h, w = rgb.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    # Segment cat head/eyes, body, and background cleanly using GrabCut seeded on cat silhouette
    mask_gc = np.full((h, w), cv2.GC_PR_BGD, dtype=np.uint8)
    mask_gc[20:h - 10, 65:w - 40] = cv2.GC_PR_FGD
    mask_gc[55:245, 125:345] = cv2.GC_FGD
    mask_gc[:, :30] = cv2.GC_BGD
    bgd_model = np.zeros((1, 65), np.float64)
    fgd_model = np.zeros((1, 65), np.float64)
    cv2.grabCut(cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR), mask_gc, None, bgd_model, fgd_model, 4, cv2.GC_INIT_WITH_MASK)
    cat_fg = np.isin(mask_gc, [cv2.GC_FGD, cv2.GC_PR_FGD])

    # Separate focal eyes & nose zone inside cat foreground
    eyes_zone = cat_fg & (((xx - 205) ** 2 / (115 ** 2) + (yy - 125) ** 2 / (75 ** 2)) <= 1.0)
    cat_body = cat_fg & (~eyes_zone)
    bg_mask = (~cat_fg)
    objects = [
        ('Room backdrop', bg_mask.astype(np.uint8) * 255, True),
        ('Cat fur & coat', cat_body.astype(np.uint8) * 255, False),
        ('Cat face & eyes', eyes_zone.astype(np.uint8) * 255, False),
    ]
    m_path = _write_manifest_bundle(folder, rgb, objects, backend='opencv-grabcut', amodal_bg=False)
    rois = [
        ('Left Eye & Whiskers', (130, 82, 96, 96)),
        ('Bridge & Fur Striping', (205, 78, 96, 96)),
    ]
    return m_path, rgb, rois


def _make_comic_poster_1024x1536(folder: Path) -> tuple[Path, np.ndarray, list[tuple[str, tuple[int, int, int, int]]]]:
    """Synthesize a rich 1024x1536 cel-shaded comic illustration with lineart, glazes, and highlights."""
    w, h = 1024, 1536
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)

    # 1. Atmospheric sunset-to-indigo sky + architectural horizon background
    t_sky = np.clip(yy / float(h), 0.0, 1.0)
    bg = np.zeros((h, w, 3), dtype=np.float32)
    bg[:, :, 0] = (1.0 - t_sky) * 34.0 + t_sky * 238.0
    bg[:, :, 1] = (1.0 - t_sky) * 52.0 + t_sky * 124.0
    bg[:, :, 2] = (1.0 - t_sky) * 112.0 + t_sky * 96.0
    # Warm sun halo behind character
    sun_d = np.hypot(xx - 520.0, yy - 540.0)
    sun_glow = np.exp(-(sun_d ** 2) / (2.0 * 240.0 ** 2))
    bg[:, :, 0] = np.clip(bg[:, :, 0] + 95.0 * sun_glow, 0, 255)
    bg[:, :, 1] = np.clip(bg[:, :, 1] + 75.0 * sun_glow, 0, 255)
    bg[:, :, 2] = np.clip(bg[:, :, 2] + 35.0 * sun_glow, 0, 255)

    canvas = np.clip(bg, 0, 255).astype(np.uint8)
    bgr = cv2.cvtColor(canvas, cv2.COLOR_RGB2BGR)

    # Foreground Mask 1: Flowing crimson cape & shoulder silhouette
    cape_mask = np.zeros((h, w), dtype=np.uint8)
    cape_pts = np.array([
        [210, 1535], [275, 870], [410, 725], [635, 735], [790, 910], [865, 1535]
    ], dtype=np.int32)
    cv2.fillPoly(cape_mask, [cape_pts], 255)

    # Foreground Mask 2: Character head, hair, face, eyes & collar
    char_mask = np.zeros((h, w), dtype=np.uint8)
    cv2.ellipse(char_mask, (512, 565), (175, 215), 0, 0, 360, 255, -1)
    hair_pts = np.array([
        [310, 610], [335, 360], [460, 275], [610, 295], [715, 420],
        [725, 650], [655, 520], [580, 445], [445, 440], [365, 535]
    ], dtype=np.int32)
    cv2.fillPoly(char_mask, [hair_pts], 255)
    cape_mask[char_mask > 0] = 0

    # Paint Cape with cel shading + warm rim highlight + dark fold lines
    cape_bool = cape_mask > 0
    cape_rgb = np.zeros((h, w, 3), dtype=np.float32)
    cape_rgb[:, :, 0] = 178.0 - 55.0 * np.clip((xx - 350.0) / 420.0, 0.0, 1.0)
    cape_rgb[:, :, 1] = 36.0 + 18.0 * np.clip((1200.0 - yy) / 500.0, 0.0, 1.0)
    cape_rgb[:, :, 2] = 54.0 + 28.0 * np.clip((xx - 400.0) / 400.0, 0.0, 1.0)
    # Cel shadow fold
    fold = ((xx - 470.0) + 0.22 * (yy - 800.0) > 65.0) & ((xx - 470.0) + 0.22 * (yy - 800.0) < 195.0)
    cape_rgb[fold] *= np.array([0.58, 0.52, 0.68], dtype=np.float32)
    # Rim light on left edge of cape
    rim = cape_bool & (cv2.erode(cape_mask, np.ones((15, 15), np.uint8)) == 0) & (xx < 460)
    cape_rgb[rim] = np.array([252.0, 195.0, 150.0], dtype=np.float32)

    rgb_work = canvas.copy()
    rgb_work[cape_bool] = np.clip(cape_rgb[cape_bool], 0, 255).astype(np.uint8)

    # Paint Character face, hair, eyes, shading, and crisp hero ink lineart
    face_ellipse = np.zeros((h, w), dtype=np.uint8)
    cv2.ellipse(face_ellipse, (512, 575), (142, 178), 0, 0, 360, 255, -1)
    hair_only = (char_mask > 0) & (face_ellipse == 0)
    face_bool = (char_mask > 0) & (face_ellipse > 0)

    # Warm peach skin with soft cheek blush + jaw multiply shadow
    skin = np.zeros((h, w, 3), dtype=np.float32)
    skin[:, :, 0] = 246.0 - 18.0 * np.clip((xx - 420.0) / 220.0, 0.0, 1.0)
    skin[:, :, 1] = 208.0 - 24.0 * np.clip((yy - 480.0) / 260.0, 0.0, 1.0)
    skin[:, :, 2] = 188.0 - 20.0 * np.clip((xx - 420.0) / 220.0, 0.0, 1.0)
    jaw_shadow = face_bool & ((xx - 545.0) * 0.8 + (yy - 635.0) * 0.6 > 25.0)
    skin[jaw_shadow] *= np.array([0.82, 0.76, 0.84], dtype=np.float32)
    rgb_work[face_bool] = np.clip(skin[face_bool], 0, 255).astype(np.uint8)

    # Deep ultramarine-slate hair with teal sheen highlight band
    hair_col = np.zeros((h, w, 3), dtype=np.float32)
    hair_col[:, :, 0] = 32.0 + 18.0 * np.clip((600.0 - yy) / 300.0, 0.0, 1.0)
    hair_col[:, :, 1] = 44.0 + 36.0 * np.clip((600.0 - yy) / 300.0, 0.0, 1.0)
    hair_col[:, :, 2] = 78.0 + 48.0 * np.clip((600.0 - yy) / 300.0, 0.0, 1.0)
    sheen = hair_only & (np.abs(yy - (385.0 + 0.12 * (xx - 500.0))) < 18.0)
    hair_col[sheen] = np.array([128.0, 196.0, 238.0], dtype=np.float32)
    rgb_work[hair_only] = np.clip(hair_col[hair_only], 0, 255).astype(np.uint8)

    # Expressive anime/comic eyes + specular catchlights + tapered ink linework
    bgr_work = cv2.cvtColor(rgb_work, cv2.COLOR_RGB2BGR)
    for ex in (452, 572):
        cv2.ellipse(bgr_work, (ex, 565), (28, 20), 0, 0, 360, (248, 248, 252), -1, cv2.LINE_AA)
        cv2.circle(bgr_work, (ex, 566), 15, (145, 78, 38), -1, cv2.LINE_AA)
        cv2.circle(bgr_work, (ex, 566), 8, (24, 18, 22), -1, cv2.LINE_AA)
        cv2.circle(bgr_work, (ex - 6, 559), 5, (255, 255, 255), -1, cv2.LINE_AA)
        cv2.ellipse(bgr_work, (ex, 556), (31, 16), 0, 185, 355, (18, 16, 24), 5, cv2.LINE_AA)
        cv2.ellipse(bgr_work, (ex, 534), (30, 8), -6 if ex < 500 else 6, 190, 350, (24, 22, 32), 3, cv2.LINE_AA)

    # Nose, mouth, jawline, and cape ink contours (strictly inside foreground masks)
    cv2.polylines(bgr_work, [np.array([[508, 592], [500, 622], [514, 628]], np.int32)], False, (32, 24, 30), 3, cv2.LINE_AA)
    cv2.ellipse(bgr_work, (512, 666), (22, 7), 0, 10, 170, (42, 28, 36), 3, cv2.LINE_AA)
    cv2.ellipse(bgr_work, (512, 575), (138, 174), 0, 20, 160, (20, 18, 26), 4, cv2.LINE_AA)
    for fx0, fy0, fx1, fy1 in [(365, 920, 315, 1490), (515, 860, 505, 1500), (655, 910, 725, 1490)]:
        cv2.line(bgr_work, (fx0, fy0), (fx1, fy1), (22, 16, 24), 5, cv2.LINE_AA)

    rgb_final = cv2.cvtColor(bgr_work, cv2.COLOR_BGR2RGB)
    bg_mask = ((cape_mask == 0) & (char_mask == 0)).astype(np.uint8) * 255

    objects = [
        ('Sunset sky backdrop', bg_mask, True),
        ('Crimson cloak', cape_mask, False),
        ('Hero character portrait', char_mask, False),
    ]
    m_path = _write_manifest_bundle(folder, rgb_final, objects, backend='sam3-image', amodal_bg=False)
    rois = [
        ('Hero Eyes & Catchlight', (412, 518, 196, 112)),
        ('Hair Sheen & Jaw Contour', (350, 345, 196, 150)),
    ]
    return m_path, rgb_final, rois


class MetricSuite:
    def __init__(self) -> None:
        self.device = torch.device('cpu')
        self.lpips_fn = None
        self.dists_fn = None
        try:
            import lpips
            self.lpips_fn = lpips.LPIPS(net='alex', verbose=False).to(self.device).eval()
        except Exception:
            pass
        try:
            from DISTS_pytorch import DISTS
            self.dists_fn = DISTS().to(self.device).eval()
        except Exception:
            pass

    def evaluate(self, ref_rgb: np.ndarray, pred_rgba: np.ndarray) -> dict:
        # Composite pred_rgba over white if any tiny transparent pixel exists, though full coverage has alpha=255
        alpha = pred_rgba[:, :, 3:4].astype(np.float32) / 255.0
        pred_rgb = np.clip(pred_rgba[:, :, :3].astype(np.float32) * alpha + 255.0 * (1.0 - alpha), 0, 255).astype(np.uint8)

        # 1. CIEDE2000 perceptual color difference
        ref_lab = skcolor.rgb2lab(ref_rgb)
        pred_lab = skcolor.rgb2lab(pred_rgb)
        de00 = float(np.mean(skcolor.deltaE_ciede2000(ref_lab, pred_lab)))

        # 2. Lab-ETF edge direction cosine similarity on salient edges
        r_tx, r_ty, r_mag = compute_lab_etf(ref_rgb, iterations=2)
        p_tx, p_ty, _ = compute_lab_etf(pred_rgb, iterations=2)
        edge_mask = r_mag > 0.12
        if np.any(edge_mask):
            cos_sim = float(np.mean(np.abs(r_tx[edge_mask] * p_tx[edge_mask] + r_ty[edge_mask] * p_ty[edge_mask])))
        else:
            cos_sim = 1.0

        # 3. LPIPS & DISTS
        lpips_val = None
        dists_val = None
        with torch.no_grad():
            r_t = torch.from_numpy(ref_rgb.astype(np.float32) / 255.0).permute(2, 0, 1).unsqueeze(0).to(self.device)
            p_t = torch.from_numpy(pred_rgb.astype(np.float32) / 255.0).permute(2, 0, 1).unsqueeze(0).to(self.device)
            if max(ref_rgb.shape[:2]) > 768:
                scale = 768.0 / float(max(ref_rgb.shape[:2]))
                nh = int(round(ref_rgb.shape[0] * scale))
                nw = int(round(ref_rgb.shape[1] * scale))
                r_eval = torch.nn.functional.interpolate(r_t, size=(nh, nw), mode='bilinear', align_corners=False)
                p_eval = torch.nn.functional.interpolate(p_t, size=(nh, nw), mode='bilinear', align_corners=False)
            else:
                r_eval, p_eval = r_t, p_t
            if self.lpips_fn is not None:
                lpips_val = round(float(self.lpips_fn(p_eval * 2.0 - 1.0, r_eval * 2.0 - 1.0).item()), 4)
            if self.dists_fn is not None:
                dists_val = round(float(self.dists_fn(p_eval, r_eval).item()), 4)

        return {
            'deltaE00': round(de00, 3),
            'etfCosine': round(cos_sim, 4),
            'lpips': lpips_val,
            'dists': dists_val,
        }


def _inspect_ora(ora_path: Path) -> dict:
    with zipfile.ZipFile(ora_path) as zf:
        stack = ET.fromstring(zf.read('stack.xml'))
        layers = list(stack.find('stack'))
        ops = sorted({l.attrib.get('composite-op', 'svg:src-over') for l in layers})
        return {
            'layerCount': len(layers),
            'compositeOps': ops,
            'layerNames': [l.attrib.get('name', '') for l in layers],
        }


def _render_ora_without_foreground(ora_path: Path, bg_keyword: str) -> Image.Image:
    """Composite ONLY the bottom-most background object's layers from an .ora archive."""
    del bg_keyword
    with zipfile.ZipFile(ora_path) as zf:
        stack = ET.fromstring(zf.read('stack.xml'))
        w, h = int(stack.attrib['w']), int(stack.attrib['h'])
        layers = list(stack.find('stack'))
        is_legacy = any(l.attrib.get('name', '').endswith(' / Paint') for l in layers)
        bg_layers = []
        if is_legacy:
            # Legacy stack (front-to-back) per object: "<Name> / Paint", "Local refinement", "Base pigment", "Guides"
            for l in reversed(layers):
                bg_layers.append(l)
                if l.attrib.get('name', '').endswith(' / Paint'):
                    break
        else:
            # Artist stack (front-to-back) per object ends at "Flats · Silhouette"
            seen_first_flats = False
            for l in reversed(layers):
                lname = l.attrib.get('name', '')
                if lname.startswith('Flats'):
                    if seen_first_flats:
                        break
                    seen_first_flats = True
                bg_layers.append(l)
        canvas = np.zeros((h, w, 4), dtype=np.float32)
        for l in bg_layers:
            if l.attrib.get('visibility') == 'hidden':
                continue
            im = np.asarray(Image.open(io.BytesIO(zf.read(l.attrib['src']))).convert('RGBA'), dtype=np.float32) / 255.0
            op = float(l.attrib.get('opacity', '1'))
            sa = im[:, :, 3:4] * op
            da = canvas[:, :, 3:4]
            out_a = sa + da * (1.0 - sa)
            mode = l.attrib.get('composite-op', 'svg:src-over')
            cs = im[:, :, :3]
            cb = canvas[:, :, :3]
            if mode == 'svg:multiply':
                comp_rgb = cs * sa * (1.0 - da) + cb * da * (1.0 - sa) + (cs * cb) * sa * da
            elif mode == 'svg:screen':
                scr = 1.0 - (1.0 - cs) * (1.0 - cb)
                comp_rgb = cs * sa * (1.0 - da) + cb * da * (1.0 - sa) + scr * sa * da
            else:
                comp_rgb = cs * sa + cb * da * (1.0 - sa)
            safe_a = np.maximum(out_a, 1e-6)
            canvas[:, :, :3] = np.where(out_a > 1e-6, comp_rgb / safe_a, 0.0)
            canvas[:, :, 3:4] = out_a
        return Image.fromarray(np.clip(np.round(canvas * 255.0), 0, 255).astype(np.uint8), mode='RGBA')


def _make_checkerboard_rgba(size: tuple[int, int], tile: int = 16) -> Image.Image:
    w, h = size
    yy, xx = np.mgrid[0:h, 0:w]
    parity = ((xx // tile) + (yy // tile)) % 2
    arr = np.empty((h, w, 4), dtype=np.uint8)
    arr[:, :, 0] = np.where(parity == 0, 244, 206)
    arr[:, :, 1] = np.where(parity == 0, 246, 212)
    arr[:, :, 2] = np.where(parity == 0, 250, 224)
    arr[:, :, 3] = 255
    return Image.fromarray(arr, mode='RGBA')


def _make_labeled_strip(panels: list[tuple[str, str, Image.Image]], thumb_w: int = 380) -> Image.Image:
    scaled = []
    for title, subtitle, im in panels:
        rgba = im.convert('RGBA')
        checker = _make_checkerboard_rgba(rgba.size)
        comp = Image.alpha_composite(checker, rgba).convert('RGB')
        scale = thumb_w / float(comp.width)
        th = max(1, int(round(comp.height * scale)))
        resized = comp.resize((thumb_w, th), Image.Resampling.LANCZOS)
        scaled.append((title, subtitle, resized))

    header_h = 54
    pad = 14
    max_h = max(im.height for _, _, im in scaled)
    total_w = pad + len(scaled) * (thumb_w + pad)
    total_h = header_h + max_h + pad
    out = Image.new('RGB', (total_w, total_h), (20, 24, 33))
    draw = ImageDraw.Draw(out)
    font = ImageFont.load_default()
    for idx, (title, subtitle, im) in enumerate(scaled):
        x = pad + idx * (thumb_w + pad)
        draw.text((x, 10), title, fill=(240, 245, 255), font=font)
        draw.text((x, 28), subtitle, fill=(156, 172, 198), font=font)
        out.paste(im, (x, header_h))
    return out


def _make_zoom_400_strip(
    panels: list[tuple[str, Image.Image]], crop_box: tuple[int, int, int, int], crop_label: str
) -> Image.Image:
    cx, cy, cw, ch = crop_box
    zoomed = []
    for title, im in panels:
        rgba = im.convert('RGBA')
        bg = Image.new('RGBA', rgba.size, (245, 246, 250, 255))
        rgb = Image.alpha_composite(bg, rgba).convert('RGB')
        x1 = min(rgb.width, cx + cw)
        y1 = min(rgb.height, cy + ch)
        patch = rgb.crop((cx, cy, x1, y1))
        up = patch.resize((patch.width * 4, patch.height * 4), Image.Resampling.NEAREST)
        zoomed.append((title, f'{crop_label} · 400% Nearest', up))
    return _make_labeled_strip(zoomed, thumb_w=max(260, cw * 3))


def run_harness(engine: Path, output_dir: Path, fast: bool = False) -> dict:
    engine = engine.resolve()
    output_dir = output_dir.resolve()
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    metrics = MetricSuite()
    cases = [
        ('coffee_600x400', _make_coffee_manifest),
        ('chelsea_451x300', _make_chelsea_manifest),
        ('comic_cel_1024x1536', _make_comic_poster_1024x1536),
    ]

    summary: dict = {'generatedAt': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'cases': {}, 'superResolution': {}}

    for case_name, builder in cases:
        case_dir = output_dir / case_name
        manifest_dir = case_dir / 'manifest_bundle'
        manifest_path, ref_rgb, rois = builder(manifest_dir)
        case_results = {}
        strip_panels: list[tuple[str, str, Image.Image]] = [
            ('Reference Source', f'{ref_rgb.shape[1]}x{ref_rgb.shape[0]} RGB', Image.fromarray(ref_rgb))
        ]
        zoom_panels: list[tuple[str, Image.Image]] = [('Source', Image.fromarray(ref_rgb))]

        baseline_ora_for_amodal: Path | None = None
        for planner in ('sweep', 'multiscale', 'artist'):
            out_run = case_dir / f'run_{planner}'
            t0 = time.monotonic()
            cmd = [str(engine), 'run', '--manifest', str(manifest_path), '--output', str(out_run),
                   '--accept-all', '--planner', planner]
            if planner == 'artist':
                cmd.append('--amodal-background')
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            elapsed = round(time.monotonic() - t0, 2)
            if res.returncode != 0:
                reason = (res.stderr.strip() or f'exit {res.returncode}').splitlines()[-1]
                if planner == 'artist':
                    raise RuntimeError(f'{case_name} artist failed: {res.stderr}')
                # Legacy planner hit hard scanline/fragment budget limit on complex/high-res scene:
                # record authentic failure evidence panel rather than aborting benchmark.
                fail_im = Image.new('RGBA', (ref_rgb.shape[1], ref_rgb.shape[0]), (42, 18, 24, 255))
                d_fail = ImageDraw.Draw(fail_im)
                d_fail.text((18, 24), f'LEGACY {planner.upper()} FAILED', fill=(255, 150, 160))
                d_fail.text((18, 46), f'Error: {reason}', fill=(240, 210, 215))
                case_results[planner] = {
                    'status': f'FAILED ({reason})',
                    'strokeEvents': None,
                    'deltaE00': None,
                    'lpips': None,
                    'dists': None,
                    'etfCosine': None,
                    'replaySourceAssetReads': None,
                    'elapsedSec': elapsed,
                    'ora': {'layerCount': 0, 'compositeOps': [], 'layerNames': []},
                }
                strip_panels.append((f'C++ --planner {planner} [FAILED]', reason, fail_im))
                zoom_panels.append((f'{planner} [FAILED]', fail_im))
                continue
            rep = json.loads(res.stdout)
            assert rep['replaySourceAssetReads'] == 0
            assert rep['reopenReplayMatches'] is True
            final_im = Image.open(out_run / 'final.png').convert('RGBA')
            m = metrics.evaluate(ref_rgb, np.asarray(final_im))
            ora_info = _inspect_ora(out_run / 'painted.ora')
            m.update({
                'status': 'PASSED',
                'strokeEvents': int(rep['strokeEvents']),
                'replaySourceAssetReads': int(rep['replaySourceAssetReads']),
                'elapsedSec': elapsed,
                'ora': ora_info,
            })
            case_results[planner] = m
            if planner in ('sweep', 'multiscale') and baseline_ora_for_amodal is None:
                baseline_ora_for_amodal = out_run / 'painted.ora'
            sub = f"strokes={m['strokeEvents']}  dE00={m['deltaE00']}  LPIPS={m['lpips']}"
            strip_panels.append((f'C++ --planner {planner}', sub, final_im))
            zoom_panels.append((planner, final_im))

        # Run Phase 3 PyTorch Differentiable Perceptual Optimizer -> C++ compile-plan
        opt_out = case_dir / 'run_artist_opt'
        t0 = time.monotonic()
        opt_cfg = DifferentiableArtistConfig(
            steps=18 if fast else 32,
            max_strokes_per_object=110 if fast else 150,
            amodal_background=True,
            use_perceptual_loss=True,
            cpp_residual_pass=True,
        )
        opt_rep = optimize_manifest_to_native(manifest_path, engine, opt_out, config=opt_cfg, allow_fixture=True)
        elapsed_opt = round(time.monotonic() - t0, 2)
        assert opt_rep['replaySourceAssetReads'] == 0
        opt_im = Image.open(opt_out / 'final.png').convert('RGBA')
        m_opt = metrics.evaluate(ref_rgb, np.asarray(opt_im))
        m_opt.update({
            'status': 'PASSED',
            'strokeEvents': int(opt_rep['strokeEvents']),
            'replaySourceAssetReads': int(opt_rep['replaySourceAssetReads']),
            'elapsedSec': elapsed_opt,
            'ora': _inspect_ora(opt_out / 'painted.ora'),
        })
        case_results['artist-opt'] = m_opt
        sub_opt = f"strokes={m_opt['strokeEvents']}  dE00={m_opt['deltaE00']}  LPIPS={m_opt['lpips']}"
        strip_panels.append(('PyTorch DiffOpt + C++', sub_opt, opt_im))
        zoom_panels.append(('artist-opt', opt_im))

        # Save A/B comparison strip + 400% focal zoom crops + ORA receipt copy
        _make_labeled_strip(strip_panels).save(output_dir / f'{case_name}_ab_strip.png')
        for r_idx, (r_label, r_box) in enumerate(rois):
            _make_zoom_400_strip(zoom_panels, r_box, r_label).save(output_dir / f'{case_name}_zoom{r_idx + 1}_400pct.png')
        shutil.copyfile(case_dir / 'run_artist/painted.ora', output_dir / f'{case_name}_artist.ora')

        # Amodal foreground-hidden proof strip (legacy cutout hole vs artist seamless background)
        bg_artist = _render_ora_without_foreground(case_dir / 'run_artist/painted.ora', 'backdrop')
        if baseline_ora_for_amodal is not None and baseline_ora_for_amodal.is_file():
            bg_legacy = _render_ora_without_foreground(baseline_ora_for_amodal, 'backdrop')
            legacy_title = f'Legacy ({baseline_ora_for_amodal.parent.name.replace("run_", "")} FG Hidden)'
            legacy_sub = 'Occluded cutout hole exposed!'
        else:
            # Show raw unoccluded background mask cutout when both legacy planners exceeded budget
            raw_bg_mask = np.frombuffer((manifest_dir / 'masks/layer-00.u8').read_bytes(), dtype=np.uint8).reshape(ref_rgb.shape[:2])
            cutout = np.zeros((ref_rgb.shape[0], ref_rgb.shape[1], 4), dtype=np.uint8)
            cutout[raw_bg_mask > 0, :3] = ref_rgb[raw_bg_mask > 0]
            cutout[raw_bg_mask > 0, 3] = 255
            bg_legacy = Image.fromarray(cutout, mode='RGBA')
            legacy_title = 'Disjoint Background Mask (FG Hidden)'
            legacy_sub = 'Occluded foreground hole!'
        amodal_strip = _make_labeled_strip([
            (legacy_title, legacy_sub, bg_legacy),
            ('Artist Amodal (FG Hidden)', 'Seamless painted background (0 source reads)', bg_artist),
        ])
        amodal_strip.save(output_dir / f'{case_name}_amodal_proof.png')
        summary['cases'][case_name] = case_results

    # Phase 4: Native 1024x1536 -> 3300x5100 (300 DPI) Super-Resolution Verification!
    comic_manifest = output_dir / 'comic_cel_1024x1536/manifest_bundle/manifest.json'
    sr_dir = output_dir / 'superres_3300x5100_300dpi'
    t0 = time.monotonic()
    sr_res = subprocess.run([
        str(engine), 'run', '--manifest', str(comic_manifest),
        '--output', str(sr_dir), '--accept-all', '--planner', 'artist',
        '--amodal-background', '--target-size', '3300x5100', '--target-dpi', '300',
    ], capture_output=True, text=True, timeout=360)
    if sr_res.returncode != 0:
        raise RuntimeError(f'3300x5100 super-resolution failed: {sr_res.stderr}')
    sr_elapsed = round(time.monotonic() - t0, 2)
    sr_rep = json.loads(sr_res.stdout)
    assert sr_rep['width'] == 3300 and sr_rep['height'] == 5100
    assert sr_rep['replaySourceAssetReads'] == 0

    low_artist_im = Image.open(output_dir / 'comic_cel_1024x1536/run_artist/final.png').convert('RGB')
    bicubic_3300 = low_artist_im.resize((3300, 5100), Image.Resampling.BICUBIC)
    native_3300 = Image.open(sr_dir / 'final.png').convert('RGB')

    # Measure edge gradient sharpness (99th percentile Sobel magnitude) on 3300x5100 eye/ink crop
    sx = 3300.0 / 1024.0
    sy = 5100.0 / 1536.0
    eye_box_300dpi = (int(425 * sx), int(525 * sy), 320, 220)
    b_crop = np.asarray(bicubic_3300.crop((eye_box_300dpi[0], eye_box_300dpi[1],
                                           eye_box_300dpi[0] + eye_box_300dpi[2],
                                           eye_box_300dpi[1] + eye_box_300dpi[3])))
    n_crop = np.asarray(native_3300.crop((eye_box_300dpi[0], eye_box_300dpi[1],
                                          eye_box_300dpi[0] + eye_box_300dpi[2],
                                          eye_box_300dpi[1] + eye_box_300dpi[3])))
    b_grad = float(np.percentile(np.hypot(
        cv2.Sobel(cv2.cvtColor(b_crop, cv2.COLOR_RGB2GRAY), cv2.CV_32F, 1, 0),
        cv2.Sobel(cv2.cvtColor(b_crop, cv2.COLOR_RGB2GRAY), cv2.CV_32F, 0, 1),
    ), 99.0))
    n_grad = float(np.percentile(np.hypot(
        cv2.Sobel(cv2.cvtColor(n_crop, cv2.COLOR_RGB2GRAY), cv2.CV_32F, 1, 0),
        cv2.Sobel(cv2.cvtColor(n_crop, cv2.COLOR_RGB2GRAY), cv2.CV_32F, 0, 1),
    ), 99.0))

    # Dedicated DryInk Physical Brush A/B Comparison Strip (Legacy engine12 vs Upgraded Physical engine12)
    dry_panels = []
    for label, sub, use_phys in [
        ('Legacy DryInk (engine12)', 'Fixed 2.6px parallel wire tracks', False),
        ('Upgraded Physical DryInk', 'World paperTooth + depletion + clumping', True),
    ]:
        d_plan = output_dir / f'dry_plan_{int(use_phys)}.json'
        d_out = output_dir / f'dry_run_{int(use_phys)}'
        d_plan.write_text(json.dumps({
            'schema': 'drawborad.stroke-plan.v1',
            'width': 320,
            'height': 180,
            'dpi': 300,
            'background': {'transparent': False, 'paperRgb': [0.97, 0.96, 0.93]},
            'objects': [{
                'name': 'Calligraphy Study',
                'layers': [{
                    'name': 'Dry Ink',
                    'stage': 'artist.hero_ink' if use_phys else 'paint',
                    'blend': 'normal',
                    'opacity': 1.0,
                    'strokes': [
                        {
                            'engine': 12,
                            'tool': 'dry-ink',
                            'width': 22.0,
                            'opacity': 0.96,
                            'color': [0.09, 0.08, 0.11, 1.0],
                            'seed': 20261118,
                            'physicalDryInk': use_phys,
                            'linearOptical': use_phys,
                            'points': [[24.0, 52.0, 0.92], [115.0, 42.0, 0.68], [210.0, 58.0, 0.36], [296.0, 48.0, 0.14]],
                        },
                        {
                            'engine': 12,
                            'tool': 'dry-ink',
                            'width': 2.4,
                            'opacity': 0.95,
                            'color': [0.14, 0.10, 0.12, 1.0],
                            'seed': 998877,
                            'physicalDryInk': use_phys,
                            'linearOptical': use_phys,
                            'points': [[28.0, 128.0, 0.85], [160.0, 118.0, 0.52], [292.0, 134.0, 0.18]],
                        },
                    ],
                }],
            }],
        }), encoding='utf-8')
        subprocess.run([str(engine), 'compile-plan', '--plan', str(d_plan), '--output', str(d_out)],
                       check=True, capture_output=True, text=True)
        d_im = Image.open(d_out / 'final.png').convert('RGBA')
        dry_panels.append((label, sub, d_im.resize((d_im.width * 2, d_im.height * 2), Image.Resampling.NEAREST)))
        d_plan.unlink(missing_ok=True)
        shutil.rmtree(d_out, ignore_errors=True)
    _make_labeled_strip(dry_panels, thumb_w=540).save(output_dir / 'dry_ink_ab_200pct.png')

    sr_zoom = _make_labeled_strip([
        ('1024x1536 Bicubic Upscaled', f'Blurred raster edges (p99 grad={b_grad:.1f})',
         Image.fromarray(b_crop).resize((b_crop.shape[1] * 2, b_crop.shape[0] * 2), Image.Resampling.NEAREST)),
        ('Native 3300x5100 300 DPI C++', f'Crisp analytic AA + paper tooth (p99 grad={n_grad:.1f})',
         Image.fromarray(n_crop).resize((n_crop.shape[1] * 2, n_crop.shape[0] * 2), Image.Resampling.NEAREST)),
    ], thumb_w=520)
    sr_zoom.save(output_dir / 'superres_3300x5100_300dpi_zoom.png')
    shutil.copyfile(sr_dir / 'painted.ora', output_dir / 'superres_3300x5100_300dpi.ora')

    summary['superResolution'] = {
        'sourceSize': [1024, 1536],
        'targetSize': [3300, 5100],
        'dpi': 300,
        'strokeEvents': int(sr_rep['strokeEvents']),
        'paintLayers': int(sr_rep['paintLayers']),
        'replaySourceAssetReads': int(sr_rep['replaySourceAssetReads']),
        'elapsedSec': sr_elapsed,
        'bicubicP99EdgeGrad': round(b_grad, 2),
        'native300DpiP99EdgeGrad': round(n_grad, 2),
        'ora': _inspect_ora(sr_dir / 'painted.ora'),
    }

    (output_dir / 'ab_report.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')

    # Write concise Markdown summary table
    md_lines = [
        '# Drawborad Cloud-First Artist Quality A/B Evidence Report',
        '',
        '| Case | Planner | Status | Strokes | CIEDE2000 (↓) | LPIPS (↓) | DISTS (↓) | ETF Cos (↑) | ORA Ops | Source Reads |',
        '|---|---|---|---:|---:|---:|---:|---:|---|---:|',
    ]
    for cname, cdata in summary['cases'].items():
        for pname, m in cdata.items():
            ops_str = ','.join(m['ora']['compositeOps']) if m['ora']['compositeOps'] else '-'
            md_lines.append(
                f"| `{cname}` | `{pname}` | {m['status']} | {m['strokeEvents']} | {m['deltaE00']} | {m['lpips']} | "
                f"{m['dists']} | {m['etfCosine']} | `{ops_str}` | {m['replaySourceAssetReads']} |"
            )
    sr = summary['superResolution']
    md_lines += [
        '',
        '## 300 DPI Native Super-Resolution (`1024x1536 -> 3300x5100`)',
        f"- Output dimensions: `{sr['targetSize'][0]}x{sr['targetSize'][1]}` @ `{sr['dpi']} DPI` (`16,830,000` pixels)",
        f"- Total strokes: `{sr['strokeEvents']}` across `{sr['paintLayers']}` native artist layers (`{','.join(sr['ora']['compositeOps'])}`)",
        f"- Authoritative reopen `replaySourceAssetReads`: `{sr['replaySourceAssetReads']}`",
        f"- 300 DPI Eye/Ink Crop p99 Sobel Edge Sharpness: Bicubic `{sr['bicubicP99EdgeGrad']}` -> Native C++ `{sr['native300DpiP99EdgeGrad']}`",
        '',
    ]
    (output_dir / 'ab_report.md').write_text('\n'.join(md_lines), encoding='utf-8')

    # Clean intermediate temporary run subdirectories so ab-evidence stays compact & inspectable
    for cname, _ in cases:
        shutil.rmtree(output_dir / cname, ignore_errors=True)
    shutil.rmtree(sr_dir, ignore_errors=True)

    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fast', action='store_true')
    args = parser.parse_args()
    report = run_harness(args.engine, args.output, fast=args.fast)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
