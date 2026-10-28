"""Lossless RGBA cutouts, preview and interoperable OpenRaster archive."""
from __future__ import annotations
import io
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

import numpy as np
from PIL import Image, ImageOps

from .manifest import Document


def source_rgba(document: Document) -> Image.Image:
    with Image.open(document.image_path) as f:
        return ImageOps.exif_transpose(f).convert('RGBA')


def rgba_layer(document: Document, index: int, source: Image.Image | None = None) -> Image.Image:
    if index < 0 or index >= len(document.layers):
        raise IndexError('Object layer not found')
    if source is None:
        source = source_rgba(document)
    mask = document.read_mask(document.layers[index])
    image = source.copy()
    alpha = np.asarray(source.getchannel('A')).copy()
    alpha[~mask] = 0
    image.putalpha(Image.fromarray(alpha, mode='L'))
    return image


def png_bytes(image: Image.Image) -> bytes:
    output = io.BytesIO()
    image.save(output, format='PNG')
    return output.getvalue()


def preview_png(document: Document) -> bytes:
    source = source_rgba(document)
    merged = Image.new('RGBA', source.size)
    for index in range(len(document.layers)):
        merged = Image.alpha_composite(merged, rgba_layer(document, index, source))
    return png_bytes(merged)


def ora_bytes(document: Document) -> bytes:
    source = source_rgba(document)
    root = ET.Element('image', {'version':'0.0.3','w':str(document.width),'h':str(document.height),
                                'name':'Drawborad semantic object layers'})
    stack = ET.SubElement(root, 'stack')
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w') as z:
        # OpenRaster requires mimetype to be the first uncompressed entry.
        z.writestr('mimetype', b'image/openraster', compress_type=zipfile.ZIP_STORED)
        for i in reversed(range(len(document.layers))):
            name = f'data/layer-{i:02d}.png'
            z.writestr(name, png_bytes(rgba_layer(document, i, source)), compress_type=zipfile.ZIP_DEFLATED)
            ET.SubElement(stack, 'layer', {'name':document.layers[i].name,
                'src':name,'opacity':'1.0','visibility':'visible',
                'composite-op':'svg:src-over','x':'0','y':'0'})
        z.writestr('stack.xml', ET.tostring(root, encoding='utf-8', xml_declaration=True))
        z.writestr('mergedimage.png', preview_png(document))
        thumb = Image.open(io.BytesIO(preview_png(document)))
        thumb.thumbnail((256,256))
        z.writestr('Thumbnails/thumbnail.png', png_bytes(thumb))
    return output.getvalue()