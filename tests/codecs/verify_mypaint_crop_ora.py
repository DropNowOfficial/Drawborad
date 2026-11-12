"""Independent ZIP/PNG reader; verifies real exported pixels, not renderer code."""
import io
import sys
import zipfile
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.testzip() is None
    root = ET.fromstring(z.read('stack.xml'))
    assert (int(root.attrib['w']), int(root.attrib['h'])) == (3300, 5100)
    layers = list(root.find('stack').findall('layer'))
    assert len(layers) == 2
    top = layers[0]
    assert top.attrib['composite-op'] == 'svg:multiply'
    assert float(top.attrib['opacity']) == .5
    assert int(top.attrib.get('x', 0)) == int(top.attrib.get('y', 0)) == 0
    def pixels(name):
        with Image.open(io.BytesIO(z.read(name))) as image:
            assert image.size == (3300, 5100)
            return np.array(image.convert('RGBA'))
    actual = pixels(top.attrib['src'])
    expected = np.zeros_like(actual)
    expected[509:515,510:518] = [128,64,200,128]
    expected[509,510,3] = 0
    assert np.array_equal(actual, expected), 'layer ROI offset/data mismatch'
    del actual, expected
    background = pixels(layers[1].attrib['src'])
    assert np.all(background == [51,102,204,255])
    # Independent integer Multiply for opaque background, effective alpha round(128*.5)=64.
    fg = np.array([128,64,200], dtype=np.uint64)
    bg = np.array([51,102,204], dtype=np.uint64)
    den = 255*255*255
    color = (bg*255*191*255 + fg*bg*64*255 + den//2)//den
    background[509:515,510:518,:3] = color
    background[509,510] = [51,102,204,255]
    merged = pixels('mergedimage.png')
    assert np.array_equal(merged, background), 'merged Multiply/opacity or location mismatch'
print('PASS independent ORA ZIP/PNG: 3300x5100, two editable layers, exact ROI and full-image recomposition')
