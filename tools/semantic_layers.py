#!/usr/bin/env python3
"""Visible-only semantic layers from Drawborad's recorded six-region coffee fixture.

This consumes *existing* manually arranged native proposal masks recorded in
Drawborad's repository. It does NOT run a model, infer a label, or fill areas
hidden behind foreground objects. Output is RGBA pixel cutouts, not brushwork.
"""
from __future__ import annotations

from collections import OrderedDict
from io import BytesIO
from pathlib import Path
import hashlib
import json
import zipfile
from xml.etree import ElementTree as ET

import numpy as np
from PIL import Image

SOURCE_SIZE = (600, 400)
SOURCE_HASH = 'cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7'
BACKGROUND_HASH = '7cad8503712b27343bcbcc61c2120fb4d5eac0e1949189cffe94a2497523708b'


def _sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _decode_recorded_mask(path: Path) -> np.ndarray:
    item = json.loads(path.read_text(encoding='utf-8'))
    if (item['width'], item['height']) != SOURCE_SIZE:
        raise ValueError(f'Unexpected mask dimensions: {path}')
    first = item['first_value']
    runs = item['runs']
    if first not in (0, 255) or not isinstance(runs, list) or not runs or any(
            not isinstance(n, int) or n <= 0 for n in runs) or sum(runs) != 240000:
        raise ValueError(f'Invalid mask runs: {path}')
    flat = np.empty(240000, dtype=np.uint8)
    position, value = 0, first
    for n in runs:
        flat[position:position + n] = value
        position += n
        value = 255 - value
    raw = flat.tobytes()
    if _sha(raw) != item['sha256']:
        raise ValueError(f'Mask SHA-256 mismatch: {path}')
    return flat.reshape(400, 600)


def load_object_masks(fixtures: str | Path) -> 'OrderedDict[str, np.ndarray]':
    """Map 6 reviewed visible regions to 4 distinct user-facing objects.

    Source mapping: visible saucer=1, visible cup body/handle=2,
    inner cup=3, coffee surface=4, spoon fragments=5; table/backdrop=0.
    The labels come from recorded review metadata, NOT model inference.
    """
    fixtures = Path(fixtures)
    numbered = {i: _decode_recorded_mask(fixtures / f'mask{i}.json') for i in (1, 2, 3, 4, 5)}
    count = sum((a > 0).astype(np.uint8) for a in numbered.values())
    if np.any(count > 1):
        raise ValueError('Recorded semantic regions overlap')
    background = np.where(count == 0, 255, 0).astype(np.uint8)
    if _sha(background.tobytes()) != BACKGROUND_HASH:
        raise ValueError('Recorded foreground union does not match reviewed background')
    if (fixtures / 'mask0.json').exists():
        actual = _decode_recorded_mask(fixtures / 'mask0.json')
        if not np.array_equal(actual, background):
            raise ValueError('Recorded background differs from complement')
    cup = np.where((numbered[2] > 0) | (numbered[3] > 0) | (numbered[4] > 0), 255, 0).astype(np.uint8)
    out = OrderedDict([
        ('wood_table', background),
        ('red_saucer', numbered[1]),
        ('cup', cup),
        ('spoon', numbered[5]),
    ])
    assert np.all(sum((x > 0).astype(np.uint8) for x in out.values()) == 1)
    return out


def make_rgba_layers(rgb: np.ndarray, masks: dict[str, np.ndarray]) -> 'OrderedDict[str, np.ndarray]':
    if rgb.shape != (400, 600, 3) or rgb.dtype != np.uint8:
        raise ValueError('Expected 600x400 uint8 RGB image')
    rgba: 'OrderedDict[str, np.ndarray]' = OrderedDict()
    for name, mask in masks.items():
        if mask.shape != rgb.shape[:2] or mask.dtype != np.uint8 or not np.isin(mask, [0, 255]).all():
            raise ValueError(f'Invalid binary mask for {name}')
        pixels = np.zeros((400, 600, 4), dtype=np.uint8)
        visible = mask != 0
        pixels[visible, :3] = rgb[visible]
        pixels[:, :, 3] = mask
        rgba[name] = pixels
    return rgba


def composite_layers(layers: dict[str, np.ndarray]) -> np.ndarray:
    if not layers:
        raise ValueError('No layers')
    result = np.zeros((400, 600, 4), dtype=np.uint8)
    assigned = np.zeros((400, 600), dtype=bool)
    for name, layer in layers.items():
        if layer.shape != result.shape or layer.dtype != np.uint8:
            raise ValueError(f'Invalid layer {name}')
        visible = layer[:, :, 3] > 0
        if np.any(assigned & visible):
            raise ValueError('Non-disjoint object layers cannot be composited by simple assignment')
        result[visible] = layer[visible]
        assigned |= visible
    return result


def _png(image: np.ndarray) -> bytes:
    out = BytesIO()
    Image.fromarray(image, 'RGBA').save(out, format='PNG')
    return out.getvalue()


def save_openraster(path: str | Path, layers: dict[str, np.ndarray]) -> None:
    """Write a standards-shaped four-layer ORA; top layer is spoon."""
    if len(layers) != 4:
        raise ValueError('Expected exactly four semantic object layers')
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    stack = ET.Element('image', {'w': '600', 'h': '400', 'name': 'Drawborad visible semantic object layers', 'version': '0.0.3'})
    stack_children = ET.SubElement(stack, 'stack')
    ordered_names = list(reversed(layers))
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('mimetype', b'image/openraster', compress_type=zipfile.ZIP_STORED)
        for idx, name in enumerate(ordered_names):
            layer = layers[name]
            filename = f'data/layer-{idx:02}.png'
            archive.writestr(filename, _png(layer), compress_type=zipfile.ZIP_DEFLATED)
            ET.SubElement(stack_children, 'layer', {
                'name': name, 'src': filename, 'x': '0', 'y': '0',
                'visibility': 'visible', 'opacity': '1.0', 'composite-op': 'svg:src-over',
            })
        archive.writestr('stack.xml', ET.tostring(stack, encoding='utf-8', xml_declaration=True))
        archive.writestr('mergedimage.png', _png(composite_layers(layers)))
        thumb = Image.fromarray(composite_layers(layers), 'RGBA')
        thumb.thumbnail((256, 256), Image.Resampling.LANCZOS)
        thumbnail_buffer = BytesIO()
        thumb.save(thumbnail_buffer, format='PNG')
        archive.writestr('Thumbnails/thumbnail.png', thumbnail_buffer.getvalue())
        archive.writestr('data/metadata.json', json.dumps({
            'provenance': 'Drawborad recorded six reviewed scene masks',
            'semanticAccuracyVerified': False, 'sourceVisibleOnly': True,
            'modelExecutedInThisRun': False,
        }).encode('utf-8'))