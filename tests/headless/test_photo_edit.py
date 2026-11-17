"""Exercise a supplied native photo project; preserve it and save an edited copy."""
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image

engine, project, output = map(lambda x: Path(x).resolve(), sys.argv[1:])
output.mkdir(parents=True, exist_ok=False)
proc = subprocess.Popen([str(engine), 'session'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
seq = 0
def call(method, **params):
    global seq
    seq += 1
    proc.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');proc.stdin.flush()
    response=json.loads(proc.stdout.readline());assert response['request_id']==str(seq)
    return response
def ok(method, **params):
    result=call(method,**params);assert 'result' in result,result
    return result['result']
def frame(name, **params):
    result=ok('export',path=str(output/(name+'.png')),format='png',**params)
    assert result['source_asset_reads']=='0'
    return Image.open(output/(name+'.png')).convert('RGBA').tobytes()
try:
    state=ok('open',path=str(project));assert int(state['stroke_count'])>8192
    event=next(e for e in reversed(state['strokes']) if e['tool']=='marker')
    before=frame('before')
    changed=ok('command',expected_revision=state['revision'],type='recolor_stroke',stroke_id=event['id'],color='#00ff00')
    edited=next(e for e in changed['strokes'] if e['id']==event['id'])
    assert {k:v for k,v in event.items() if k!='color'}=={k:v for k,v in edited.items() if k!='color'}
    after=frame('after');assert before!=after
    stale=call('undo',expected_revision=state['revision']);assert stale['error']['code']=='stale_revision'
    undone=ok('undo',expected_revision=changed['revision']);assert frame('undo')==before
    redone=ok('redo',expected_revision=undone['revision']);assert frame('redo')==after
    locked=ok('command',expected_revision=redone['revision'],type='set_properties',node_id=event['object_id'],locked=True)
    rejected=call('command',expected_revision=locked['revision'],type='recolor_stroke',stroke_id=event['id'],color='#ff0000')
    assert rejected['error']['code']=='locked'
    unlocked=ok('command',expected_revision=locked['revision'],type='set_properties',node_id=event['object_id'],locked=False)
    saved=ok('save',expected_revision=unlocked['revision'],path=str(output/'edited.draw'))
    ok('close',expected_revision=saved['revision'])
    reopened=ok('open',path=str(output/'edited.draw'));assert reopened['strokes']==saved['strokes']
    assert frame('reopened')==after
    assert frame('zero',event_limit='0')!=after
    frame('middle',event_limit=str(int(saved['stroke_count'])//2))
    ok('export',path=str(output/'edited.ora'),format='ora')
    (output/'receipt.json').write_text(json.dumps(dict(strokes=saved['stroke_count'],stroke_id=event['id'],recolor=True,undo=True,redo=True,stale_rejected=True,locked_rejected=True,saved_reopened=True,source_reads=0),indent=2))
finally:
    proc.stdin.close();proc.wait(timeout=20)
