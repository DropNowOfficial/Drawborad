"""Isolated non-destructive raster-mask preview primitive. NOT wired to project files.
Keeps source PNG untouched; callers must retain it and separately store mask state.
No new native format or implicit fallback. No GUI or save support claimed.
"""
from io import BytesIO
from PIL import Image, ImageChops
MAX_PIXELS = 16_777_216
MAX_BYTES = 32 * 1024 * 1024

def _image(raw, mode):
    if not isinstance(raw, bytes) or len(raw) > MAX_BYTES:
        raise ValueError('Invalid PNG bytes or byte budget')
    with Image.open(BytesIO(raw)) as source:
        if source.format != 'PNG' or source.width * source.height > MAX_PIXELS:
            raise ValueError('Only bounded PNG layers accepted')
        # Mask must explicitly encode coverage as grayscale, not ambiguous RGBA.
        if mode == 'L' and source.mode != 'L':
            raise ValueError('Mask must be grayscale coverage PNG')
        return source.convert(mode)

def _origin(value):
    if not isinstance(value, (tuple, list)) or len(value) != 2 or any(type(n) != int or n < 0 for n in value):
        raise ValueError('Origin must contain two nonnegative integers')
    return tuple(value)

def preview_masked_crop(stroke_png, mask_png, stroke_origin, mask_origin, canvas_size, enabled=True):
    """Return a derived crop, never overwrite either original. Outside mask is hidden.
    disabled returns byte-identical original after bounds validation. Masked RGB is
    preserved and only alpha changes. This models one stroke-mask, not group blend.
    """
    if type(enabled) != bool:
        raise ValueError('enabled must be boolean')
    width, height = _origin(canvas_size)
    if not width or not height or width * height > MAX_PIXELS:
        raise ValueError('Canvas exceeds preview budget')
    sx, sy = _origin(stroke_origin); mx, my = _origin(mask_origin)
    stroke = _image(stroke_png, 'RGBA'); mask = _image(mask_png, 'L')
    if sx + stroke.width > width or sy + stroke.height > height or mx + mask.width > width or my + mask.height > height:
        raise ValueError('Crop outside canvas')
    if not enabled:
        return stroke_png
    coverage = Image.new('L', stroke.size, 0)
    left, top = max(sx, mx), max(sy, my)
    right, bottom = min(sx+stroke.width, mx+mask.width), min(sy+stroke.height, my+mask.height)
    if left < right and top < bottom:
        coverage.paste(mask.crop((left-mx,top-my,right-mx,bottom-my)), (left-sx,top-sy))
    stroke.putalpha(ImageChops.multiply(stroke.getchannel('A'), coverage))
    result = BytesIO(); stroke.save(result, format='PNG'); return result.getvalue()
