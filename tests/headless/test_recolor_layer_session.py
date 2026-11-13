"""Native editable color-layer change, not a new fill/raster replacement."""
import json, subprocess, sys, tempfile
from pathlib import Path
from PIL import Image

def main(binary):
 with tempfile.TemporaryDirectory() as tmp:
  root=Path(tmp);proc=subprocess.Popen([str(Path(binary).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
  def call(method,**params):
   nonlocal seq
   seq+=1;proc.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['request_id']==str(seq);return r
  def ok(method,**params):
   r=call(method,**params);assert 'result' in r,r;return r['result']
  def export(name):
   path=root/(name+'.png');ok('export',path=str(path),format='png');return Image.open(path).convert('RGBA')
  try:
   state=ok('create',width='128',height='128')
   layers=[]
   for y,color in [(24,'#d16a40'),(88,'#101c24')]:
    old={n['id'] for n in state['nodes']};state=ok('command',type='add_fill_layer',expected_revision=state['revision']);layer=next(n['id'] for n in state['nodes'] if n['id'] not in old and n['kind']==3);layers.append(layer)
    for yy in [y,y+12]:
     state=ok('command',type='draw_stroke',layer_id=layer,points=[{'x':20,'y':yy,'pressure':.5},{'x':100,'y':yy,'pressure':1}],width=6,color=color,expected_revision=state['revision'])
   before=export('before');old=state
   state=ok('command',type='recolor_layer',layer_id=layers[0],color='#367c88',expected_revision=state['revision'])
   after=export('after');assert before.tobytes()!=after.tobytes()
   assert before.getchannel('A').tobytes()==after.getchannel('A').tobytes()
   assert before.crop((0,70,128,128)).tobytes()==after.crop((0,70,128,128)).tobytes()
   assert len(state['strokes'])==len(old['strokes'])
   for a,b in zip(old['strokes'],state['strokes']):
    assert a['id']==b['id'] and a['geometry_count']==b['geometry_count']
   stale=call('command',type='recolor_layer',layer_id=layers[0],color='#ffffff',expected_revision=old['revision']);assert 'error' in stale
   assert ok('state')==state
   state=ok('undo',expected_revision=state['revision']);assert export('undo').tobytes()==before.tobytes()
   state=ok('redo',expected_revision=state['revision']);assert export('redo').tobytes()==after.tobytes()
   state=ok('command',type='set_properties',node_id=layers[0],locked=True,expected_revision=state['revision'])
   assert 'error' in call('command',type='recolor_layer',layer_id=layers[0],color='#ffffff',expected_revision=state['revision']);assert ok('state')==state
   path=root/'changed.draw';state=ok('save_copy',path=str(path),expected_revision=state['revision']);ok('close',expected_revision=state['revision']);ok('open',path=str(path));assert export('reopen').tobytes()==after.tobytes()
   print('PASS whole native layer recolor, preserved alpha/neighbors/events, one undo, stale/lock, save/reopen')
  finally:proc.stdin.close();proc.wait(timeout=10);assert proc.returncode==0
if __name__=='__main__':main(sys.argv[1])
