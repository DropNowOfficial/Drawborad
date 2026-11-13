"""Native append RPC smoke with synthetic raster; no MyPaint runtime or artwork claim."""
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw


def main(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        image = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
        ImageDraw.Draw(image).line((10, 20, 50, 40), fill=(0, 0, 0, 255), width=4)
        source = root / 'ink.png'
        image.save(source)
        manifest = root / 'manifest.json'
        manifest.write_text(json.dumps({'engine': 'synthetic-test', 'points': [[10, 20], [50, 40]], 'replay': 'complete-test-marker'}))
        proc = subprocess.Popen([str(Path(binary).resolve()), 'session'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        sequence = 0
        def call(method, **params):
            nonlocal sequence
            sequence += 1
            proc.stdin.write(json.dumps({'request_id': str(sequence), 'method': method, 'params': params}) + '\n')
            proc.stdin.flush()
            with selectors.DefaultSelector() as selector:
                selector.register(proc.stdout, selectors.EVENT_READ)
                assert selector.select(120), method + ' timeout'
            result = json.loads(proc.stdout.readline())
            assert result['request_id'] == str(sequence)
            return result
        def ok(method, **params):
            response = call(method, **params)
            assert 'result' in response, response
            return response['result']
        def pixels(name):
            path = root / (name + '.png')
            ok('export', path=str(path), format='png')
            with Image.open(path) as result:
                return result.convert('RGBA').tobytes()
        try:
            args = dict(path=str(source), manifest_path=str(manifest), expected_revision='0', expected_document_id='0'*32)
            assert 'error' in call('append_brush_layer', **args)
            state = ok('create', width='64', height='64')
            assert 'append_brush_layer' in state['capabilities']
            baseline = pixels('baseline')
            args['expected_revision'] = state['revision']
            args['expected_document_id'] = state['document_id']
            wrong_document = dict(args, expected_document_id='0'*32)
            assert call('append_brush_layer', **wrong_document)['error']['code'] == 'stale_revision'
            assert ok('state') == state
            missing_document = dict(args)
            del missing_document['expected_document_id']
            assert 'error' in call('append_brush_layer', **missing_document)
            assert ok('state') == state
            before = state
            empty = root / 'empty.png'
            Image.new('RGBA', (64, 64), (25, 50, 75, 0)).save(empty)
            rejected = call('append_brush_layer', **dict(args, path=str(empty)))
            assert rejected.get('error', {}).get('message') == 'brush-layer.no-visible-ink', rejected
            assert ok('state') == before, 'empty brush layer must not change the document'
            state = ok('append_brush_layer', **args)
            added = [n for n in state['nodes'] if n['id'] not in {x['id'] for x in before['nodes']}]
            assert len(added) == 1 and added[0]['locked'] and added[0]['blend'] == 1
            assert added[0]['name'] == 'MyPaint ink (experimental raster)'
            assert state['strokes'] == before['strokes'], 'raster must not pretend to be native editable strokes'
            after = pixels('painted')
            assert after != baseline
            expected = Image.alpha_composite(Image.frombytes('RGBA', (64, 64), baseline), image).tobytes()
            assert after == expected
            assert call('append_brush_layer', **args)['error']['code'] == 'stale_revision'
            state = ok('undo', expected_revision=state['revision'])
            assert pixels('undo') == baseline
            state = ok('redo', expected_revision=state['revision'])
            assert pixels('redo') == after
            # Bad input must not alter either revision or node list.
            for bad in ['not-json', '[' + '0,' * 600000 + '0]']:
                manifest.write_text(bad)
                assert 'error' in call('append_brush_layer', path=str(source), manifest_path=str(manifest), expected_revision=state['revision'], expected_document_id=state['document_id'])
                assert ok('state') == state
            manifest.write_text('{}')
            wrong = root / 'wrong.png'
            Image.new('RGBA', (32, 32)).save(wrong)
            assert 'error' in call('append_brush_layer', path=str(wrong), manifest_path=str(manifest), expected_revision=state['revision'], expected_document_id=state['document_id'])
            assert ok('state') == state
            target = root / 'saved.draw'
            state = ok('save_copy', path=str(target), expected_revision=state['revision'])
            stored = target.read_bytes()
            assert b'complete-test-marker' in stored and source.read_bytes() in stored
            ok('close', expected_revision=state['revision'])
            reopened = ok('open', path=str(target))
            assert reopened['nodes'] == state['nodes']
            assert pixels('reopened') == after
            print('PASS native append raster: revision guards, one undo/redo, immutable PNG+manifest saved/reopened, invalid inputs unchanged')
        finally:
            proc.stdin.close()
            proc.wait(timeout=10)
            assert proc.returncode == 0

if __name__ == '__main__':
    main(sys.argv[1])
