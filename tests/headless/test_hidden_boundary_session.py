"""Normal cropped RPC synthetic regression; requires newly compiled native binary."""
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
        ImageDraw.Draw(image).line((0, 1, 7, 4), fill=(255, 255, 255, 255), width=4)
        source = root / 'ink.png'
        image.save(source)
        manifest = root / 'manifest.json'
        meta={'prototype':'mypaint-cropped-study-v1','full_canvas':[520,520],'origin':[510,509],'roi_size':[8,6],'original_global_points':[{'x':512,'y':512,'pressure':.3,'time_ms':0}],'translated_local_points':[{'x':2,'y':3,'pressure':.3,'time_ms':0}],'png_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'worker_manifest':{'canvas':[8,6],'points':[{'x':2,'y':3,'pressure':.3,'time_ms':0}]},'replay':'complete-test-marker'}
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
            source=root/'lineart.png';lineart=Image.new('RGBA',(16,16),'white');draw=ImageDraw.Draw(lineart);draw.rectangle((3,3,12,12),outline='black');lineart.putpixel((12,8),(160,160,160,255));lineart.save(source)
            state=ok('import_lineart',path=str(source));ink=next(n['id'] for n in state['nodes'] if n['stage']=='drawborad.lineart.rgba.v1')
            state=ok('command',type='add_fill_layer',expected_revision=state['revision']);fill=next(n['id'] for n in state['nodes'] if n['name']=='Color fill')
            assert 'hidden_fill_boundary' in state['capabilities'] and state['hidden_boundaries']==[]
            def identity():return dict(expected_revision=state['revision'],expected_document_id=state['document_id'])
            args=dict(lineart_id=ink,points=[{'x':12,'y':7},{'x':12,'y':10}],radius=1)
            wrong=identity();wrong['expected_document_id']='0'*32;assert 'error'in call('add_hidden_boundary',**args,**wrong);assert ok('state')==state
            stale=identity();stale['expected_revision']='0';assert 'error'in call('add_hidden_boundary',**args,**stale);assert ok('state')==state
            original=pixels('original');state=ok('add_hidden_boundary',**args,**identity());assert pixels('with-helper')==original
            boundary=state['hidden_boundaries'][0];assert boundary['enabled'] and boundary['lineart_id']==ink
            state=ok('toggle_hidden_boundary',boundary_id=boundary['id'],enabled=False,**identity());assert not state['hidden_boundaries'][0]['enabled']
            state=ok('undo',expected_revision=state['revision']);assert state['hidden_boundaries'][0]['enabled']
            state=ok('hidden_boundary_fill',lineart_id=ink,layer_id=fill,x=6,y=8,source_luminance_threshold=120,color='#FF0000',**identity());filled=pixels('filled');assert filled!=original
            state=ok('undo',expected_revision=state['revision']);assert pixels('undone')==original
            state=ok('redo',expected_revision=state['revision']);assert pixels('redone')==filled
            target=root/'boundary.draw';state=ok('save_copy',path=str(target),expected_revision=state['revision']);assert b'DRAW-DELTA-16'in target.read_bytes() and source.read_bytes()in target.read_bytes()
            ok('close',expected_revision=state['revision']);reopened=ok('open',path=str(target));assert reopened['hidden_boundaries']==state['hidden_boundaries'];assert pixels('reopened')==filled
            print('PASS native hidden helper identity/invisible/toggle/fill/undo/save-reopen')
        finally:
            proc.stdin.close();proc.wait(timeout=10);assert proc.returncode==0
if __name__=='__main__':main(sys.argv[1])
