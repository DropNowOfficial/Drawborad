"""Semantic & focal ROI importance weighting + Lab-space Edge Tangent Flow."""
from __future__ import annotations

import cv2
import numpy as np


def compute_lab_etf(rgb_u8: np.ndarray, iterations: int = 3) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute smooth CIE Lab Edge Tangent Flow (tx, ty, normalized_magnitude)."""
    lab = cv2.cvtColor(rgb_u8, cv2.COLOR_RGB2LAB).astype(np.float32)
    jxx = np.zeros(lab.shape[:2], dtype=np.float32)
    jxy = np.zeros(lab.shape[:2], dtype=np.float32)
    jyy = np.zeros(lab.shape[:2], dtype=np.float32)
    weights = (1.0, 0.75, 0.75)
    for ch, w in enumerate(weights):
        gx = cv2.Sobel(lab[:, :, ch], cv2.CV_32F, 1, 0, ksize=3) * 0.125
        gy = cv2.Sobel(lab[:, :, ch], cv2.CV_32F, 0, 1, ksize=3) * 0.125
        jxx += w * (gx * gx)
        jxy += w * (gx * gy)
        jyy += w * (gy * gy)

    jxx = cv2.GaussianBlur(jxx, (5, 5), 1.1)
    jxy = cv2.GaussianBlur(jxy, (5, 5), 1.1)
    jyy = cv2.GaussianBlur(jyy, (5, 5), 1.1)

    trace = jxx + jyy
    diff = jxx - jyy
    disc = np.sqrt(np.maximum(0.0, diff * diff + 4.0 * jxy * jxy))
    lambda1 = 0.5 * (trace + disc)

    gx_dom = lambda1 - jyy
    gy_dom = jxy
    fallback = (np.abs(gx_dom) + np.abs(gy_dom)) < 1e-7
    gx_dom = np.where(fallback, jxy, gx_dom)
    gy_dom = np.where(fallback, lambda1 - jxx, gy_dom)

    tx = -gy_dom
    ty = gx_dom
    norm = np.hypot(tx, ty)
    tx = np.where(norm > 1e-7, tx / np.maximum(norm, 1e-7), 1.0).astype(np.float32)
    ty = np.where(norm > 1e-7, ty / np.maximum(norm, 1e-7), 0.0).astype(np.float32)
    mag = np.sqrt(np.maximum(0.0, lambda1)).astype(np.float32)
    mag /= max(float(mag.max()), 1e-5)

    # Vectorized Kang-Lee-Chui ETF bilateral tangent smoothing
    for _ in range(iterations):
        acc_x = np.zeros_like(tx)
        acc_y = np.zeros_like(ty)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if dx * dx + dy * dy > 5:
                    continue
                q_tx = np.roll(np.roll(tx, dy, axis=0), dx, axis=1)
                q_ty = np.roll(np.roll(ty, dy, axis=0), dx, axis=1)
                q_mag = np.roll(np.roll(mag, dy, axis=0), dx, axis=1)
                dot = tx * q_tx + ty * q_ty
                phi = np.where(dot >= 0.0, 1.0, -1.0).astype(np.float32)
                wm = 0.5 * (1.0 + np.tanh(3.5 * (q_mag - mag)))
                wd = np.maximum(0.08, np.abs(dot))
                w_tot = phi * wm * wd
                acc_x += w_tot * q_tx
                acc_y += w_tot * q_ty
        n = np.hypot(acc_x, acc_y)
        valid = n > 1e-6
        tx = np.where(valid, acc_x / np.maximum(n, 1e-6), tx)
        ty = np.where(valid, acc_y / np.maximum(n, 1e-6), ty)

    return tx, ty, mag


def compute_semantic_roi_weights(
    rgb_u8: np.ndarray,
    object_masks: list[np.ndarray],
    object_names: list[str],
    is_background: list[bool],
) -> np.ndarray:
    """Build [H, W] perceptual importance map giving 5x-8x weight to key semantic/focal details."""
    h, w = rgb_u8.shape[:2]
    weights = np.ones((h, w), dtype=np.float32)

    gray = cv2.cvtColor(rgb_u8, cv2.COLOR_RGB2GRAY).astype(np.float32) / 255.0
    lap = np.abs(cv2.Laplacian(gray, cv2.CV_32F, ksize=3))
    lap_smooth = cv2.GaussianBlur(lap, (9, 9), 2.2)
    lap_norm = lap_smooth / max(float(np.percentile(lap_smooth, 99.0)), 1e-4)
    lap_norm = np.clip(lap_norm, 0.0, 1.5)

    # Local contrast / fine-feature density (eyes, facial features, lettering, crisp lineart)
    local_mean = cv2.GaussianBlur(gray, (15, 15), 4.0)
    local_var = np.sqrt(np.maximum(0.0, cv2.GaussianBlur((gray - local_mean) ** 2, (15, 15), 4.0)))
    local_var /= max(float(np.percentile(local_var, 98.5)), 1e-4)
    focal = np.clip(0.55 * lap_norm + 0.45 * local_var, 0.0, 1.5)

    focal_keywords = ('face', 'eye', 'head', 'hand', 'person', 'girl', 'boy', 'character',
                      'hero', 'title', 'text', 'logo', 'cup', 'cat', 'portrait')
    for mask, name, bg in zip(object_masks, object_names, is_background):
        m = mask > 0
        if not np.any(m):
            continue
        if not bg:
            weights[m] += 1.5
            # Silhouette boundary precision boost
            eroded = cv2.erode(m.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
            dilated = cv2.dilate(m.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
            boundary = dilated & (~eroded)
            weights[boundary] += 2.0
        lower = name.lower()
        if any(k in lower for k in focal_keywords):
            weights[m] += 2.5

    weights += 3.5 * focal
    return np.clip(weights, 1.0, 8.5).astype(np.float32)
