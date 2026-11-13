"""Distance-based taper supplies useful mouse line weight, not invented texture."""
import json,subprocess,sys,tempfile
from pathlib import Path
from PIL import Image
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);p=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
 def call(method,**params):
  global seq
  seq+=1;p.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');p.stdin.flush();return json.loads(p.stdout.readline())
 def ok(method,**params):
  r=call(method,**params);assert 'result'in r,r;return r['result']
 def png(name):
  f=root/(name+'.png');ok('export',path=str(f),format='png');return Image.open(f).convert('RGBA')
 try:
  s=ok('create',width='256',height='256');s=ok('command',type='add_fill_layer',expected_revision=s['revision']);layer=next(n['id'] for n in s['nodes'] if n['kind']==3)
  body=dict(type='draw_stroke',layer_id=layer,brush='smooth_ink',width=32,color='#202020',points=[dict(x=32,y=128,pressure=1),dict(x=224,y=128,pressure=1)])
  s=ok('command',**body,expected_revision=s['revision']);plain=png('plain');s=ok('undo',expected_revision=s['revision'])
  s=ok('command',**body,pressure_taper=.2,expected_revision=s['revision']);taper=png('taper');assert s['strokes'][-1]['geometry_count']=='4';assert plain.tobytes()!=taper.tobytes()
  assert plain.getpixel((32,138))[3]>0 and taper.getpixel((32,138))[3]==0;assert taper.getpixel((128,138))[3]>0
  s=ok('undo',expected_revision=s['revision'])
  dense=dict(body,points=[dict(x=32+i*12,y=128,pressure=1) for i in range(17)])
  s=ok('command',**dense,pressure_taper=.2,expected_revision=s['revision']);assert png('dense').tobytes()==taper.tobytes(),'sample-density invariant for same mouse centerline'
  s=ok('undo',expected_revision=s['revision']);s=ok('command',**body,pressure_taper=0,expected_revision=s['revision']);assert png('off').tobytes()==plain.tobytes()
  s=ok('undo',expected_revision=s['revision']);dot=dict(body,points=[dict(x=80,y=80,pressure=1)])
  s=ok('command',**dot,expected_revision=s['revision']);plain_dot=png('dot');s=ok('undo',expected_revision=s['revision']);s=ok('command',**dot,pressure_taper=.2,expected_revision=s['revision']);assert png('taper-dot').tobytes()==plain_dot.tobytes()
  s=ok('undo',expected_revision=s['revision']);s=ok('command',**body,pressure_taper=.2,expected_revision=s['revision']);assert png('restored').tobytes()==taper.tobytes()
  for v in [-.1,.51,'0.2',True]:
   assert 'error'in call('command',**body,pressure_taper=v,expected_revision=s['revision']);assert ok('state')==s
  s=ok('save_copy',path=str(root/'taper.draw'),expected_revision=s['revision']);ok('close',expected_revision=s['revision']);s=ok('open',path=str(root/'taper.draw'));assert png('reopen').tobytes()==taper.tobytes()
  print('PASS sparse two-point taper, narrower ends/full center, invalid inputs, save/reopen')
 finally:p.stdin.close();p.wait(timeout=10)
