"""Strict, source-backed semantic layer manifest validation.

The same schema is consumed by src/ui/SemanticManifest.cpp; never represent
pixel coverage as proof of semantic detection correctness.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageOps, UnidentifiedImageError

MAX_PIXELS = 4 * 1024 * 1024
MAX_MASKS = 64
MAX_SOURCE_BYTES = 32 * 1024 * 1024
MAX_MANIFEST_BYTES = 256 * 1024
_MASK_PATH = re.compile(r"masks/layer-\d{2}\.u8\Z")
_ALLOWED_IMAGE = {'source.png', 'source.jpg', 'source.jpeg'}
_ALLOWED_BACKENDS = {'sam3-image', 'fixture-no-ai', 'opencv-grabcut'}


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class Layer:
    name: str
    score: float
    is_background: bool
    path: Path
    count: int


@dataclass(frozen=True)
class Document:
    root: Path
    manifest: dict
    image_path: Path
    width: int
    height: int
    layers: tuple[Layer, ...]

    def read_mask(self, layer: Layer) -> np.ndarray:
        return np.frombuffer(layer.path.read_bytes(), dtype=np.uint8).reshape(self.height, self.width) != 0

    @property
    def is_ai(self) -> bool:
        return self.manifest['backend'] == 'sam3-image'


def _local_file(root: Path, relative: str, allowed: bool) -> Path:
    if not isinstance(relative, str) or not allowed or not relative.isascii() or '\\' in relative:
        raise ManifestError('Invalid manifest file name')
    child = root / relative
    if child.is_symlink() or not child.is_file() or child.resolve().is_relative_to(root.resolve()) is False:
        raise ManifestError('Missing, linked, or unsafe file')
    return child


def load_manifest(folder: Path) -> Document:
    folder = Path(folder).resolve()
    entry = folder / 'manifest.json'
    if entry.is_symlink() or not entry.is_file() or entry.stat().st_size > MAX_MANIFEST_BYTES:
        raise ManifestError('Missing or oversized manifest')
    try:
        data = json.loads(entry.read_text(encoding='utf-8'))
    except (ValueError, UnicodeError) as exc:
        raise ManifestError('Malformed manifest JSON') from exc
    if not isinstance(data, dict) or data.get('schema') != 1 or data.get('backend') not in _ALLOWED_BACKENDS or data.get('promptOrder') != 'back-to-front':
        raise ManifestError('Unsupported semantic manifest schema')
    w, h = data.get('width'), data.get('height')
    if type(w) is not int or type(h) is not int or min(w,h) < 1 or max(w,h) > 4096 or w*h > MAX_PIXELS:
        raise ManifestError('Invalid image dimensions')
    source_name = data.get('image')
    source = _local_file(folder, source_name, source_name in _ALLOWED_IMAGE)
    if source.stat().st_size > MAX_SOURCE_BYTES:
        raise ManifestError('Source image is too large')
    if hashlib.sha256(source.read_bytes()).hexdigest() != data.get('sourceSha256'):
        raise ManifestError('Source image SHA256 mismatch')
    try:
        with Image.open(source) as raw:
            if raw.format not in ('PNG', 'JPEG'):
                raise ManifestError('Unsupported source image format')
            oriented = ImageOps.exif_transpose(raw)
            if oriented.size != (w,h):
                raise ManifestError('Incorrect oriented source image dimensions')
            visible = np.asarray(oriented.convert('RGBA').getchannel('A')) > 0
            alpha = np.asarray(oriented.convert('RGBA').getchannel('A'))
            if np.any((alpha != 0) & (alpha != 255)):
                raise ManifestError('Partial source alpha cannot be represented by binary object masks')
    except (UnidentifiedImageError, OSError, Image.DecompressionBombError) as exc:
        raise ManifestError('Invalid source image') from exc
    items = data.get('objects')
    if not isinstance(items, list) or not 1 <= len(items) <= MAX_MASKS or (len(items) * w*h) > 56*1024*1024:
        raise ManifestError('Invalid number of object masks')
    occupied = np.zeros((h,w), dtype=bool)
    layers = []
    backgrounds = 0
    names = set()
    for index, row in enumerate(items):
        if not isinstance(row, dict):
            raise ManifestError('Malformed object')
        name = row.get('name')
        score = row.get('score')
        background = row.get('isBackground')
        if not isinstance(name, str) or not name.strip() or len(name) > 128 or name in names:
            raise ManifestError('Invalid or duplicate object name')
        if type(score) not in (int, float) or not math.isfinite(score) or not 0 <= score <= 1:
            raise ManifestError('Invalid confidence value')
        if type(background) is not bool:
            raise ManifestError('Missing background declaration')
        backgrounds += int(background)
        if backgrounds > 1 or (background and index != 0):
            raise ManifestError('Background must be first and unique')
        mask_name = row.get('mask')
        mask_path = _local_file(folder, mask_name, isinstance(mask_name, str) and bool(_MASK_PATH.fullmatch(mask_name)))
        if mask_path.stat().st_size != w*h:
            raise ManifestError('Object mask shape is invalid')
        raw_mask = mask_path.read_bytes()
        if hashlib.sha256(raw_mask).hexdigest() != row.get('sha256'):
            raise ManifestError('Object mask hash mismatch')
        pixels = np.frombuffer(raw_mask, dtype=np.uint8).reshape(h,w)
        if np.any((pixels != 0) & (pixels != 255)):
            raise ManifestError('Object mask must be strictly binary')
        yes = pixels > 0
        count = int(np.count_nonzero(yes))
        if count < 1 or type(row.get('pixels')) is not int or count != row['pixels']:
            raise ManifestError('Object mask has incorrect pixel count')
        if np.any(occupied & yes):
            raise ManifestError('Objects overlap: independent layers require exclusive visible pixels')
        occupied |= yes
        names.add(name)
        layers.append(Layer(name, float(score), background, mask_path, count))
    if not np.array_equal(occupied, visible):
        raise ManifestError('Object layers do not cover all and only visible source pixels')
    return Document(folder, data, source, w, h, tuple(layers))
