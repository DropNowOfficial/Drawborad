"""Conservative experiment eligibility, not a painting-quality certification.

These observable limits were defined after the seven-fixture v6 evaluation.
They have not been validated as a general photo/illustration classifier.
"""
import numpy as np


def preflight(rgba, masks):
    reasons = []
    reports = []
    report = dict(experimental=True, eligible=False, rejectionReasons=reasons, objects=reports,
                  contract="Opaque, compact-palette flat artwork only; requires existing reviewed nonoverlapping binary masks.",
                  qualityStatus="not-evaluated", naturalProcessVerified=False,
                  warning="Eligibility is not quality acceptance. Small symbols/colors can be simplified even on eligible inputs. No fallback or resizing occurs.")
    if not isinstance(rgba,np.ndarray) or rgba.dtype!=np.uint8 or rgba.ndim!=3 or rgba.shape[2]!=4:
        reasons.append("source-must-be-rgba8")
        return report
    h, w = rgba.shape[:2]
    if w < 1 or h < 1 or w > 512 or h > 512 or not isinstance(masks,(list,tuple)) or not 1 <= len(masks) <= 16:
        reasons.append("prototype-dimension-or-object-budget")
        return report
    claimed=np.zeros((h,w),bool)
    for index,raw in enumerate(masks):
        if not isinstance(raw,np.ndarray) or raw.dtype!=np.uint8 or raw.shape!=(h,w) or not np.isin(raw,(0,255)).all():
            reasons.append(f"object-{index}:mask-must-be-matching-binary-u8")
            return report
        present=raw!=0
        if not present.any():
            reasons.append(f"object-{index}:empty-mask")
            return report
        if np.any(present&claimed):
            reasons.append(f"object-{index}:overlapping-mask")
            return report
        claimed|=present
    rgb = rgba[:, :, :3].astype(np.int16)
    for index, raw in enumerate(masks):
        mask = raw != 0
        count = int(mask.sum())
        if not count:
            reasons.append(f"object-{index}:empty-mask")
            continue
        nonopaque = int(np.sum(rgba[:, :, 3][mask] != 255))
        _, histogram = np.unique(rgb[mask] // 16, axis=0, return_counts=True)
        concentration = float(np.sort(histogram)[-16:].sum() / count)
        interior = mask.copy();interior[[0, -1], :] = False;interior[:, [0, -1]] = False
        variation = np.zeros((h, w), np.int16)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            shifted = np.roll(np.roll(rgb, dy, axis=0), dx, axis=1)
            interior &= np.roll(np.roll(mask, dy, axis=0), dx, axis=1)
            variation = np.maximum(variation, np.abs(rgb - shifted).max(axis=2))
        smooth = float(np.sum(interior & (variation > 0) & (variation <= 6)) / max(1, interior.sum()))
        if nonopaque:reasons.append(f"object-{index}:nonopaque-reviewed-pixels")
        if concentration < .8:reasons.append(f"object-{index}:palette-too-distributed")
        if smooth > .35:reasons.append(f"object-{index}:smooth-gradient-content")
        reports.append(dict(object=index, acceptedPixels=count, nonopaquePixels=nonopaque,
                            top16QuantizedRgbFraction=concentration, smoothVariationFraction=smooth))
    report["eligible"]=not reasons
    return report
