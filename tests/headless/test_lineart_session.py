"""Real native PNG-import smoke. Requires rebuilt engine; no mocked results.

Usage: python test_lineart_session.py ENGINE [OPTIONAL_REAL_LINEART.png]
Optional artwork gets baseline import/save/reopen validation only, never filling.
The default 512px closed-region fixture is technical evidence, not user artwork.
"""
import hashlib
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw


def fixture(path):
    image = Image.new('RGBA', (512, 512), 'white')
    draw = ImageDraw.Draw(image)
    # Native-resolution black boundaries with explicit gray edge pixels.
    draw.rectangle((40, 40, 470, 450), outline=(128, 128, 128, 255), width=5)
    draw.rectangle((41, 41, 469, 449), outline='black', width=3)
    draw.line((255, 40, 255, 450), fill=(128, 128, 128, 255), width=5)
    draw.line((255, 40, 255, 450), fill='black', width=3)
    image.putpixel((5, 5), (70, 20, 200, 0))
    image.putpixel((6, 5), (70, 20, 200, 128))
    image.save(path, dpi=(300, 300))


def main(binary, actual=None):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / 'closed-regions.png'
        fixture(source)
        proc = subprocess.Popen([str(Path(binary).resolve()), 'session'],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        sequence = 0

        def call(method, **params):
            nonlocal sequence
            sequence += 1
            proc.stdin.write(json.dumps(dict(request_id=str(sequence), method=method, params=params))+'\n')
            proc.stdin.flush()
            with selectors.DefaultSelector() as selector:
                selector.register(proc.stdout, selectors.EVENT_READ)
                assert selector.select(120), f'Native {method} timeout'
            result = json.loads(proc.stdout.readline())
            assert result['request_id'] == str(sequence)
            return result

        def ok(method, **params):
            result = call(method, **params)
            assert 'result' in result, result
            return result['result']

        def pixels(name):
            target = root / (name+'.png')
            ok('export', path=str(target), format='png')
            with Image.open(target) as exported:
                assert exported.size == dimensions
                return exported.convert('RGBA').tobytes()

        def run_case(path, label, do_fill):
            nonlocal dimensions
            original_bytes = path.read_bytes()
            with Image.open(path) as image:
                rgba = image.convert('RGBA')
                dimensions = rgba.size
                expected = Image.alpha_composite(Image.new('RGBA', dimensions, 'white'), rgba).tobytes()
            state = ok('import_lineart', path=str(path.resolve()))
            assert (int(state['width']), int(state['height'])) == dimensions
            assert 'ora' not in state['capabilities']
            baseline = pixels(label+'-baseline')
            assert baseline == expected, 'Imported pixels must exactly match original over white'
            before_nodes = {n['id'] for n in state['nodes']}
            ink = [n for n in state['nodes'] if n['stage'] == 'drawborad.lineart.rgba.v1']
            assert len(ink) == 1 and ink[0]['locked']
            old_events = state['strokes']
            after = baseline
            if do_fill:
                state = ok('command', type='add_fill_layer', expected_revision=state['revision'])
                layers = [n for n in state['nodes'] if n['id'] not in before_nodes and n['kind'] == 3]
                assert len(layers) == 1
                state = ok('command', type='bucket_fill', layer_id=layers[0]['id'],
                           x=120, y=200, tolerance=0, color='#26a6f2', expected_revision=state['revision'])
                assert state['strokes'][:-1] == old_events
                after = pixels(label+'-filled')
                changed = 0
                for y in range(512):
                    for x in range(512):
                        pos = (y*512+x)*4
                        a, b = baseline[pos:pos+4], after[pos:pos+4]
                        if a != b:
                            changed += 1
                            assert 40 < x < 255 and 40 < y < 450, 'Fill leaked outside selected closed region'
                        if a[:3] != b'\xff\xff\xff':
                            assert a == b, 'Black/gray line or transparent-case pixels changed'
                assert changed > 50000
                state = ok('undo', expected_revision=state['revision'])
                assert pixels(label+'-undo') == baseline
                state = ok('redo', expected_revision=state['revision'])
                assert pixels(label+'-redo') == after
            copy = root / (label+'.draw')
            saved = ok('save_copy', path=str(copy), expected_revision=state['revision'])
            assert not saved['dirty']
            ok('close', expected_revision=saved['revision'])
            reopened = ok('open', path=str(copy))
            assert reopened['strokes'] == saved['strokes']
            assert pixels(label+'-reopened') == after
            assert path.read_bytes() == original_bytes
            ok('close', expected_revision=reopened['revision'])
            return {'case': label, 'size': dimensions, 'filled': do_fill,
                    'baseline_sha256': hashlib.sha256(baseline).hexdigest(),
                    'saved_reopened_sha256': hashlib.sha256(after).hexdigest(),
                    'original_png_unchanged': True}

        dimensions = None
        try:
            receipts = [run_case(source, 'technical-512', True)]
            if actual:
                receipts.append(run_case(Path(actual), 'real-lineart-baseline-only', False))
            print(json.dumps({'native_lineart_smoke': 'passed', 'cases': receipts,
                              'limitation': 'Tolerance 0; no anti-alias halo removal claim'}))
        finally:
            proc.stdin.close()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit('Usage: test_lineart_session.py ENGINE [OPTIONAL_REAL_LINEART.png]')
    main(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else None)
