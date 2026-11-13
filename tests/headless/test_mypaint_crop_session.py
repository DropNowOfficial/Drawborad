"""Cropped native RPC test with synthetic raster, not actual MyPaint or artwork."""
import json
import hashlib
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw


def main(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        image = Image.new('RGBA', (8, 6), (0, 0, 0, 0))
        ImageDraw.Draw(image).line((0, 1, 7, 4), fill=(0, 0, 0, 255), width=4)
        source = root / 'ink.png'
        image.save(source)
        manifest = root / 'manifest.json'
        meta={'prototype':'mypaint-cropped-study-v1','full_canvas':[3300,5100],'origin':[510,509],'roi_size':[8,6],'original_global_points':[{'x':512,'y':512,'pressure':.3,'time_ms':0}],'translated_local_points':[{'x':2,'y':3,'pressure':.3,'time_ms':0}],'png_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'worker_manifest':{'canvas':[8,6],'points':[{'x':2,'y':3,'pressure':.3,'time_ms':0}]},'replay':'complete-test-marker'}
        manifest.write_text(json.dumps(meta))
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
            args = dict(path=str(source), manifest_path=str(manifest), expected_revision='0', expected_document_id='0'*32,origin_x='510',origin_y='509')
            assert 'error' in call('append_brush_crop', **args)
            state = ok('create', width='3300', height='5100')
            assert 'append_brush_crop' in state['capabilities']
            baseline = pixels('baseline')
            args['expected_revision'] = state['revision']
            args['expected_document_id'] = state['document_id']
            meta['worker_manifest']['document_id']=state['document_id']
            meta['worker_manifest']['actual_dab_guard']={'passed':True,'checked_dabs':2}
            manifest.write_text(json.dumps(meta))
            wrong_document = dict(args, expected_document_id='0'*32)
            assert call('append_brush_crop', **wrong_document)['error']['code'] == 'stale_revision'
            assert ok('state') == state
            missing_document = dict(args)
            del missing_document['expected_document_id']
            assert 'error' in call('append_brush_crop', **missing_document)
            assert ok('state') == state
            before = state
            # A valid worker manifest is not proof of nonzero final alpha.
            empty = root / 'empty.png'
            Image.new('RGBA', (8, 6), (25, 50, 75, 0)).save(empty)
            empty_meta = json.loads(json.dumps(meta))
            empty_meta['png_sha256'] = hashlib.sha256(empty.read_bytes()).hexdigest()
            empty_meta['worker_manifest']['actual_dab_guard']['checked_dabs'] = 140
            empty_manifest = root / 'empty-manifest.json'
            empty_manifest.write_text(json.dumps(empty_meta))
            rejected = call('append_brush_crop', **dict(args, path=str(empty), manifest_path=str(empty_manifest)))
            assert rejected.get('error', {}).get('message') == 'brush-layer.no-visible-ink', rejected
            assert ok('state') == before, 'empty crop must not add a layer or change revision'
            state = ok('append_brush_crop', **args)
            added = [n for n in state['nodes'] if n['id'] not in {x['id'] for x in before['nodes']}]
            assert len(added) == 1 and added[0]['locked'] and added[0]['blend'] == 1
            assert added[0]['name'] == 'MyPaint ink (experimental raster)'
            assert state['strokes'] == before['strokes'], 'raster must not pretend to be native editable strokes'
            after = pixels('painted')
            assert after != baseline
            canvas=Image.frombytes('RGBA',(3300,5100),baseline)
            canvas.alpha_composite(image,(510,509));assert after==canvas.tobytes()
            assert call('append_brush_crop', **args)['error']['code'] == 'stale_revision'
            state = ok('undo', expected_revision=state['revision'])
            assert pixels('undo') == baseline
            state = ok('redo', expected_revision=state['revision'])
            assert pixels('redo') == after
            # Bad input must not alter either revision or node list.
            for bad in ['not-json', '[' + '0,' * 600000 + '0]']:
                manifest.write_text(bad)
                assert 'error' in call('append_brush_crop', path=str(source), manifest_path=str(manifest), expected_revision=state['revision'], expected_document_id=state['document_id'],origin_x='510',origin_y='509')
                assert ok('state') == state
            manifest.write_text('{}')
            wrong = root / 'wrong.png'
            Image.new('RGBA', (32, 32)).save(wrong)
            assert 'error' in call('append_brush_crop', path=str(wrong), manifest_path=str(manifest), expected_revision=state['revision'], expected_document_id=state['document_id'],origin_x='510',origin_y='509')
            assert ok('state') == state
            target = root / 'saved.draw'
            state = ok('save_copy', path=str(target), expected_revision=state['revision'])
            stored = target.read_bytes()
            assert b'complete-test-marker' in stored and source.read_bytes() in stored and b'DRAW-DELTA-13' in stored
            ok('close', expected_revision=state['revision'])
            reopened = ok('open', path=str(target))
            assert reopened['nodes'] == state['nodes']
            assert pixels('reopened') == after
            print('PASS native cropped append raster: revision guards, one undo/redo, immutable PNG+manifest saved/reopened, invalid inputs unchanged')
        finally:
            proc.stdin.close()
            proc.wait(timeout=10)
            assert proc.returncode == 0

if __name__ == '__main__':
    main(sys.argv[1])
