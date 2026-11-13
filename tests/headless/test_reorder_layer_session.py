"""Same-parent layer ordering must change compositing, not stroke geometry."""
import json,subprocess,sys,tempfile
from pathlib import Path
from PIL import Image
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);p=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
 def call(method,**kw):
  global seq
  seq+=1;p.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=kw))+'\n');p.stdin.flush();return json.loads(p.stdout.readline())
 def ok(method,**kw):
  r=call(method,**kw);assert 'result'in r,r;return r['result']
 def center():
  f=root/f'p-{seq}.png';ok('export',path=str(f),format='png');return Image.open(f).convert('RGB').getpixel((32,32))
 try:
  s=ok('create',width='64',height='64');layers=[]
  for color in ['#ff0000','#0000ff']:
   old={n['id'] for n in s['nodes']};s=ok('command',type='add_fill_layer',expected_revision=s['revision']);layer=next(n['id']for n in s['nodes']if n['id']not in old and n['kind']==3);layers.append(layer)
   s=ok('command',type='draw_stroke',layer_id=layer,points=[dict(x=10,y=32),dict(x=55,y=32)],width=8,color=color,expected_revision=s['revision'])
  before=center();events=s['strokes'];parent=next(n for n in s['nodes']if n['id']==next(n['parent_id']for n in s['nodes']if n['id']==layers[0]));order=parent['children'];idx=order.index(layers[0]);direction='up' if idx<len(order)-1 else 'down'
  s=ok('command',type='reorder_layer',layer_id=layers[0],direction=direction,expected_revision=s['revision']);after=center();assert after!=before;assert s['strokes']==events
  s=ok('undo',expected_revision=s['revision']);assert center()==before
  s=ok('redo',expected_revision=s['revision']);assert center()==after
  assert 'error'in call('command',type='reorder_layer',layer_id=layers[0],direction='across',expected_revision=s['revision']);assert ok('state')==s
  s=ok('command',type='set_properties',node_id=layers[0],locked=True,expected_revision=s['revision']);assert 'error'in call('command',type='reorder_layer',layer_id=layers[0],direction='down',expected_revision=s['revision']);assert ok('state')==s
  s=ok('command',type='set_properties',node_id=layers[0],locked=False,expected_revision=s['revision'])
  parent_id=next(n['parent_id']for n in s['nodes']if n['id']==layers[0])
  s=ok('command',type='set_properties',node_id=parent_id,locked=True,expected_revision=s['revision'])
  assert 'error'in call('command',type='reorder_layer',layer_id=layers[0],direction='down' if direction=='up' else 'up',expected_revision=s['revision']);assert ok('state')==s
  s=ok('command',type='set_properties',node_id=parent_id,locked=False,expected_revision=s['revision'])
  order=next(n['children']for n in s['nodes']if n['id']==parent_id)
  for layer,way in [(order[0],'down'),(order[-1],'up')]:
   assert 'error'in call('command',type='reorder_layer',layer_id=layer,direction=way,expected_revision=s['revision']);assert ok('state')==s
  s=ok('save_copy',path=str(root/'order.draw'),expected_revision=s['revision']);ok('close',expected_revision=s['revision']);s=ok('open',path=str(root/'order.draw'));assert center()==after
  print('PASS reorder changes overlapping paint, preserves events, undo/redo, lock/invalid guards, save/reopen')
 finally:p.stdin.close();p.wait(timeout=10)
