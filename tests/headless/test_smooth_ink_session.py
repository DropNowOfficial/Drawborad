"""Opt-in broad antialiased native ink; legacy replay remains unchanged."""
import json,subprocess,sys,tempfile
from pathlib import Path
from PIL import Image
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);p=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
 def call(method,**params):
  global seq
  seq+=1;p.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');p.stdin.flush();return json.loads(p.stdout.readline())
 def ok(method,**params):
  r=call(method,**params);assert 'result' in r,r;return r['result']
 def png(name):
  path=root/(name+'.png');ok('export',path=str(path),format='png');return Image.open(path).convert('RGBA').tobytes()
 try:
  s=ok('create',width='256',height='256');s=ok('command',type='add_fill_layer',expected_revision=s['revision']);layer=next(n['id'] for n in s['nodes'] if n['kind']==3)
  s=ok('save_copy',path=str(root/'old.draw'),expected_revision=s['revision']);blank=png('blank')
  body=dict(type='draw_stroke',brush='smooth_ink',width=32,color='#101020',layer_id=layer,points=[dict(x=30.25,y=40.5,pressure=.2),dict(x=210.75,y=170.25,pressure=1)])
  s=ok('command',**body,expected_revision=s['revision']);assert s['strokes'][0]['tool']=='smooth-ink-aa'
  rendered=png('ink');assert blank!=rendered
  assert any(0<a<255 for a in rendered[3::4]),'broad edge has fractional pixel coverage'
  assert 'error' in call('save',path=str(root/'old.draw'),expected_revision=s['revision'])
  s=ok('save_copy',path=str(root/'new.draw'),expected_revision=s['revision']);assert b'DRAW-DELTA-19' in (root/'new.draw').read_bytes()
  s=ok('undo',expected_revision=s['revision']);assert png('undo')==blank
  s=ok('redo',expected_revision=s['revision']);assert png('redo')==rendered
  ok('close',expected_revision=s['revision'],discard=True);s=ok('open',path=str(root/'new.draw'));assert png('reopen')==rendered
  for width in (0,129):
   bad=dict(body,width=width);assert 'error' in call('command',**bad,expected_revision=s['revision'])
   assert ok('state')==s
  print('PASS broad AA, alpha edge, format19 save-copy boundary, undo/redo/reopen and rejected width')
 finally:p.stdin.close();p.wait(timeout=10)
