"""Independent OpenRaster ZIP, PNG and compositing checks; no drawing logic."""
import io
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image, ImageChops


def verify(directory):
    root = Path(directory)
    report = json.loads((root / 'report.json').read_text())
    assert report['strokeEvents'] > 0
    assert report['replaySourceAssetReads'] == 0
    assert report['wholeImageFidelityVerified'] is False
    frames = [Image.open(root / name).convert('RGBA') for name in ('zero.png', 'middle.png', 'final.png')]
    assert len({im.tobytes() for im in frames}) == 3, 'zero/middle/final must differ'
    assert frames[0].getchannel('A').getextrema() == (0, 0)
    final = frames[2]
    assert all(im.size == final.size for im in frames)
    assert (root / 'project.draw').stat().st_size > 0
    with zipfile.ZipFile(root / 'painted.ora') as archive:
        assert archive.namelist()[0] == 'mimetype'
        assert archive.read('mimetype') == b'image/openraster'
        assert archive.getinfo('mimetype').compress_type == zipfile.ZIP_STORED
        stack = ET.fromstring(archive.read('stack.xml'))
        assert (int(stack.attrib['w']), int(stack.attrib['h'])) == final.size
        layers = list(stack.find('stack'))
        assert len(layers) == report['paintLayers']
        assert all(layer.tag == 'layer' and 'Layer group' not in layer.attrib['name'] for layer in layers)
        composed = Image.new('RGBA', final.size)
        for layer in reversed(layers):
            pixels = Image.open(io.BytesIO(archive.read(layer.attrib['src']))).convert('RGBA')
            assert pixels.size == final.size
            if layer.attrib.get('visibility') == 'hidden':
                continue
            assert layer.attrib['composite-op'] == 'svg:src-over'
            opacity = float(layer.attrib['opacity'])
            pixels.putalpha(pixels.getchannel('A').point(lambda v: round(v * opacity)))
            composed = Image.alpha_composite(composed, pixels)
        difference = ImageChops.difference(composed, final)
        assert max(v[1] for v in difference.getextrema()) <= 1, 'ORA layers do not reproduce authoritative final PNG'
        merged = Image.open(io.BytesIO(archive.read('mergedimage.png'))).convert('RGBA')
        assert merged.tobytes() == final.tobytes()
    return {'oraLayerCount': len(layers), 'oraCompositeMaxError': max(v[1] for v in difference.getextrema()), 'status': 'Passed'}


if __name__ == '__main__':
    print(json.dumps(verify(sys.argv[1]), sort_keys=True))
