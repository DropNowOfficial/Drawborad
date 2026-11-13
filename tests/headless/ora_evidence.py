"""Byte-level inspection of actual native ORA exports, no browser or engine mocks."""
import io
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image


def inspect_ora(path, expected_png_pixels, size, *, lineart_rgba=None, require_fill=False):
    with zipfile.ZipFile(path) as archive:
        assert archive.read('mimetype') == b'image/openraster'
        tree = ET.fromstring(archive.read('stack.xml'))
        assert (int(tree.get('w')), int(tree.get('h'))) == size
        layers = tree.findall('.//layer')  # ORA front-to-back.
        assert layers
        decoded = []
        for layer in layers:
            with Image.open(io.BytesIO(archive.read(layer.get('src')))) as image:
                assert image.size == size
                dpi = image.info.get('dpi')
                assert dpi and all(abs(value-300) < .05 for value in dpi), dpi
                decoded.append(image.convert('RGBA').tobytes())
            assert int(layer.get('x', '0')) == int(layer.get('y', '0')) == 0
        with Image.open(io.BytesIO(archive.read('mergedimage.png'))) as image:
            assert image.size == size
            merged = image.convert('RGBA').tobytes()
        assert merged == expected_png_pixels
        fill_indices = [i for i, layer in enumerate(layers) if layer.get('name') == 'Color fill']
        if require_fill:
            assert len(fill_indices) == 1, 'Independent color layer missing'
            assert layers[fill_indices[0]].get('composite-op', 'svg:src-over') == 'svg:src-over'
        if lineart_rgba is not None:
            ink_indices = [i for i, layer in enumerate(layers) if layer.get('name') == 'Original lineart']
            assert len(ink_indices) == 1
            ink = ink_indices[0]
            assert layers[ink].get('composite-op') == 'svg:multiply'
            assert decoded[ink] == lineart_rgba, 'Original lineart pixels changed in exported leaf'
            if require_fill:
                assert ink < fill_indices[0], 'Color layer must be below ink in ORA order'
        # Independent integer Porter-Duff/Multiply recomposition of downloaded
        # layer pixels and metadata, not trusting mergedimage.png alone.
        composed = bytearray(size[0]*size[1]*4)
        for layer, source in reversed(list(zip(layers, decoded))):
            if layer.get('visibility', 'visible') == 'hidden':
                continue
            opacity = int(float(layer.get('opacity', '1'))*255+.5)
            mode = layer.get('composite-op', 'svg:src-over')
            assert mode in ('svg:src-over', 'svg:multiply'), mode
            for p in range(0, len(source), 4):
                sa = (source[p+3]*opacity+127)//255
                if not sa:
                    continue
                da = composed[p+3]
                denominator = sa*255+da*(255-sa)
                for c in range(3):
                    cs, cb = source[p+c], composed[p+c]
                    if mode == 'svg:multiply':
                        divisor = denominator*255
                        numerator = cs*sa*(255-da)*255 + cb*da*(255-sa)*255 + cs*cb*sa*da
                    else:
                        divisor = denominator
                        numerator = cs*sa*255 + cb*da*(255-sa)
                    composed[p+c] = (numerator+divisor//2)//divisor
                composed[p+3] = (denominator+127)//255
        assert bytes(composed) == merged, 'Independent ORA layer recomposition differs from PNG'
        return {'layers': len(layers), 'merged_equals_png': True,
                'independent_recomposition_equal': True, 'lineart_pixels_preserved': lineart_rgba is not None}
