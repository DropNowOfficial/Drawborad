"""Native under-ink opt-in API smoke; entirely synthetic PNG, no private source."""
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
from PIL import Image


def main(binary):
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        ink = Image.new('RGBA', (64, 48), 'white')
        for left in (4, 36):
            for y in range(4, 41):
                for x in range(left, left + 25):
                    depth = min(x-left, left+24-x, y-4, 40-y)
                    c = 0 if depth < 2 else 128 if depth == 2 else 220 if depth == 3 else 255
                    ink.putpixel((x, y), (c, c, c, 255))
        source = root / 'technical.png'
        ink.save(source)
        original = source.read_bytes()
        proc = subprocess.Popen([str(Path(binary).resolve()), 'session'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        seq = 0
        def call(method, **params):
            nonlocal seq
            seq += 1
            proc.stdin.write(json.dumps({'request_id': str(seq), 'method': method, 'params': params})+'\n')
            proc.stdin.flush()
            with selectors.DefaultSelector() as ready:
                ready.register(proc.stdout, selectors.EVENT_READ)
                assert ready.select(120), method+' timed out'
            response = json.loads(proc.stdout.readline())
            assert response['request_id'] == str(seq)
            return response
        def ok(method, **params):
            response = call(method, **params)
            assert 'result' in response, response
            return response['result']
        def picture(name):
            path = root / (name+'.png')
            ok('export', path=str(path), format='png')
            with Image.open(path) as im:
                return im.convert('RGBA').copy()
        try:
            state = ok('import_lineart', path=str(source))
            old_ids = {n['id'] for n in state['nodes']}
            state = ok('command', type='add_fill_layer', expected_revision=state['revision'])
            layer = next(n['id'] for n in state['nodes'] if n['id'] not in old_ids)
            baseline = picture('before')
            nodes = state['nodes']
            def fill_args():
                return dict(type='bucket_fill', layer_id=layer, x=12, y=20, tolerance=0, color='#00ffff', expected_revision=state['revision'])
            for value in (-1, 3, 1.5, '1', None, True):
                assert 'error' in call('command', **fill_args(), under_ink=value)
                assert ok('state') == state, 'invalid under_ink must not mutate state'
            args = fill_args();args['x'] = 7;args['y'] = 19
            assert 'error' in call('command', **args, under_ink=1)
            assert ok('state') == state
            outputs = {}
            for mode in (None, 0, 1, 2):
                args = fill_args()
                if mode is not None:
                    args['under_ink'] = mode
                state = ok('command', **args)
                assert state['nodes'] == nodes
                assert state['strokes'][-1]['tool'] == 'bucket'
                image = picture('mode-'+str(mode));outputs[mode] = image.tobytes()
                distance = mode or 0
                assert image.getpixel((7,19))[0] == (0 if distance else 220)
                assert image.getpixel((6,19))[0] == (0 if distance == 2 else 128)
                for y in range(48):
                    for x in range(64):
                        selected_interior = 8 <= x <= 24 and 8 <= y <= 36
                        if not selected_interior and ink.getpixel((x,y))[:3] == (255,255,255):
                            assert image.getpixel((x,y)) == baseline.getpixel((x,y))
                        if x >= 32:
                            assert image.getpixel((x,y)) == baseline.getpixel((x,y))
                if mode != 2:
                    state = ok('undo', expected_revision=state['revision'])
                    assert picture('undo-'+str(mode)).tobytes() == baseline.tobytes()
            assert outputs[None] == outputs[0], 'omitted default must remain exactly mode0'
            saved = ok('save_copy', path=str(root/'copy.draw'), expected_revision=state['revision'])
            ok('close', expected_revision=saved['revision'])
            ok('open', path=str(root/'copy.draw'))
            assert picture('reopened').tobytes() == outputs[2]
            assert source.read_bytes() == original
            print('PASS native under_ink default0, modes1/2, invalid inputs unchanged, gray seed rejection, white/second-region safety, undo and save/reopen')
        finally:
            proc.stdin.close()
            proc.wait(timeout=10)
            assert proc.returncode == 0

if __name__ == '__main__':
    main(sys.argv[1])
