"""Independent native cropped-layer ORA verification. Requires only Pillow."""
import io
import sys
import zipfile
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

with zipfile.ZipFile(sys.argv[1]) as archive:
    assert archive.testzip() is None
    assert archive.read('mimetype') == b'image/openraster'
    root = ET.fromstring(archive.read('stack.xml'))
    assert (int(root.attrib['w']), int(root.attrib['h'])) == (3300, 5100)
    layers = list(root.find('stack').findall('layer'))
    assert len(layers) == 2
    top = layers[0]
    assert top.attrib['composite-op'] == 'svg:multiply'
    assert float(top.attrib['opacity']) == .5
    assert (int(top.attrib['x']), int(top.attrib['y'])) == (510, 509)
    def pixels(name, size):
        with Image.open(io.BytesIO(archive.read(name))) as image:
            assert image.size == size
            return image.convert('RGBA').copy()
    actual = pixels(top.attrib['src'], (8, 6))
    expected = Image.new('RGBA', (8, 6), (128, 64, 200, 128))
    expected.putpixel((0, 0), (128, 64, 200, 0))
    assert actual.tobytes() == expected.tobytes(), 'cropped leaf bytes changed'
    background = pixels(layers[1].attrib['src'], (3300, 5100))
    assert background.getextrema() == ((51,51), (102,102), (204,204), (255,255))
    # Independent opaque-backdrop Multiply with effective alpha round(128*0.5)=64.
    bg = (51,102,204)
    fg = (128,64,200)
    den = 255*255*255
    color = tuple((cb*255*191*255 + cs*cb*64*255 + den//2)//den for cb,cs in zip(bg,fg))
    ImageDraw.Draw(background).rectangle((510,509,517,514), fill=color+(255,))
    background.putpixel((510,509), bg+(255,))
    merged = pixels('mergedimage.png', (3300,5100))
    assert merged.tobytes() == background.tobytes(), 'whole-image offset/Multiply/opacity mismatch'
print('PASS native cropped ORA: actual8x6 layer at510,509;3300x5100 canvas; exact independent whole-image recomposition')
