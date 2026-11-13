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
            backdrop=root/'black.png';Image.new('RGBA',(520,520),(0,0,0,255)).save(backdrop)
            state=ok('import_lineart',path=str(backdrop))
            meta['worker_manifest'].update(paint_mode='normal',document_id=state['document_id'],actual_dab_guard={'passed':True,'checked_dabs':2})
            manifest.write_text(json.dumps(meta))
            state=ok('append_brush_crop',path=str(source),manifest_path=str(manifest),origin_x='510',origin_y='509',paint_mode='normal',expected_revision=state['revision'],expected_document_id=state['document_id'])
            node=next(n for n in state['nodes'] if n.get('mask_supported'))
            assert node['stroke_mask'] is None and node['locked'] and 'cropped_stroke_mask' in state['capabilities']
            original=pixels('original')
            def params():return dict(layer_id=node['id'],expected_revision=state['revision'],expected_document_id=state['document_id'])
            for rect in [dict(x=-1,y=0,width=1,height=1),dict(x=0,y=0,width=0,height=1),dict(x=0.5,y=0,width=1,height=1),dict(x=519,y=0,width=2,height=1)]:
                assert 'error' in call('set_stroke_mask',rect=rect,**params());assert ok('state')==state
            wrong=params();wrong['expected_document_id']='0'*32
            assert 'error' in call('set_stroke_mask',rect=dict(x=510,y=509,width=4,height=6),**wrong);assert ok('state')==state
            stale=params();stale['expected_revision']='0'
            assert 'error' in call('set_stroke_mask',rect=dict(x=510,y=509,width=4,height=6),**stale);assert ok('state')==state
            state=ok('set_stroke_mask',rect=dict(x=510,y=509,width=4,height=6),**params())
            masked=pixels('masked');assert masked!=original
            n=next(n for n in state['nodes'] if n['id']==node['id']);assert n['locked'] and n['stroke_mask']=={'enabled':True,'rect':{'x':510,'y':509,'width':4,'height':6}}
            assert 'error' in call('toggle_stroke_mask',enabled=1,**params());assert ok('state')==state
            state=ok('toggle_stroke_mask',enabled=False,**params());assert pixels('disabled')==original
            state=ok('undo',expected_revision=state['revision']);assert pixels('undo')==masked
            target=root/'masked.draw';state=ok('save_copy',path=str(target),expected_revision=state['revision'])
            assert b'DRAW-DELTA-15' in target.read_bytes() and source.read_bytes() in target.read_bytes()
            ok('close',expected_revision=state['revision']);reopened=ok('open',path=str(target));assert reopened['nodes']==state['nodes'];assert pixels('reopened')==masked
            print('PASS native single-crop mask, strict bounds/identity, lock, toggle/undo, save/reopen')
        finally:
            proc.stdin.close();proc.wait(timeout=10);assert proc.returncode==0

if __name__=='__main__':main(sys.argv[1])
