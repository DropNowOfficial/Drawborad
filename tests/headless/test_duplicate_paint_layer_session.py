"""A hidden native-event backup must be independent, reversible and persistent."""
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
 def png(n):
  f=root/(n+'.png');ok('export',path=str(f),format='png');return Image.open(f).convert('RGBA').tobytes()
 try:
  s=ok('create',width='128',height='128');s=ok('command',type='add_fill_layer',expected_revision=s['revision']);layer=next(n['id'] for n in s['nodes'] if n['kind']==3)
  assert 'error' in call('command',type='duplicate_paint_layer',layer_id=layer,expected_revision=s['revision']);assert ok('state')==s
  s=ok('command',type='draw_stroke',layer_id=layer,points=[dict(x=20,y=40,pressure=.4),dict(x=100,y=80,pressure=1)],width=6,color='#d06030',expected_revision=s['revision'])
  before=png('before');old=s;ids={n['id'] for n in s['nodes']}
  s=ok('command',type='duplicate_paint_layer',layer_id=layer,expected_revision=s['revision']);copied=next(n for n in s['nodes'] if n['id'] not in ids);copyid=copied['id'];assert not copied['visible'] and not copied['locked'];assert png('hidden')==before
  assert s['strokes'][:len(old['strokes'])]==old['strokes'];a=old['strokes'][0];b=s['strokes'][-1]
  assert b['id']!=a['id'] and b['layer_id']==copyid and b['geometry_count']==a['geometry_count'] and b['color']==a['color']
  s=ok('undo',expected_revision=s['revision']);assert len(s['nodes'])==len(old['nodes']) and len(s['strokes'])==len(old['strokes'])
  s=ok('redo',expected_revision=s['revision']);assert png('redo')==before
  stale=call('command',type='duplicate_paint_layer',layer_id=layer,expected_revision=old['revision']);assert 'error' in stale;assert ok('state')==s
  s=ok('command',type='set_properties',node_id=layer,visible=False,expected_revision=s['revision']);s=ok('command',type='set_properties',node_id=copyid,visible=True,expected_revision=s['revision']);assert png('only-copy')==before
  s=ok('command',type='recolor_layer',layer_id=copyid,color='#208080',expected_revision=s['revision']);changed=png('changed');assert changed!=before;assert s['strokes'][0]==a
  s=ok('command',type='set_properties',node_id=copyid,locked=True,expected_revision=s['revision']);assert 'error' in call('command',type='duplicate_paint_layer',layer_id=copyid,expected_revision=s['revision']);assert ok('state')==s
  s=ok('save_copy',path=str(root/'backup.draw'),expected_revision=s['revision']);ok('close',expected_revision=s['revision']);s=ok('open',path=str(root/'backup.draw'));assert png('reopen')==changed
  assert 'error'in call('command',type='duplicate_paint_layer',layer_id=s['nodes'][0]['id'],expected_revision=s['revision'])
  ok('close',expected_revision=s['revision']);Image.new('RGBA',(32,32),'white').save(root/'raster.png');s=ok('import_lineart',path=str(root/'raster.png'));raster=next(n['id'] for n in s['nodes'] if n['stage']=='drawborad.lineart.rgba.v1')
  assert 'error'in call('command',type='duplicate_paint_layer',layer_id=raster,expected_revision=s['revision']);assert ok('state')==s
  print('PASS hidden backup clones real events, independent recolor, undo/redo, stale/lock, save/reopen')
 finally:p.stdin.close();p.wait(timeout=10)
