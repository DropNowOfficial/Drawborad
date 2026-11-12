"""Actual fine-pen session: subpixel line, undo, v10 save/reopen, ORA."""
import json,sys,subprocess,tempfile
from pathlib import Path
from PIL import Image
from ora_evidence import inspect_ora
with tempfile.TemporaryDirectory() as d:
 root=Path(d);proc=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
 def rpc(method,**params):
  global seq
  seq+=1;proc.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['request_id']==str(seq);return r
 def ok(method,**params):
  r=rpc(method,**params);assert 'result' in r,r;return r['result']
 def pixels(name):
  p=root/(name+'.png');ok('export',path=str(p),format='png');return Image.open(p).convert('RGBA').tobytes()
 try:
  state=ok('create',width='64',height='64')
  old={n['id'] for n in state['nodes']};state=ok('command',type='add_fill_layer',expected_revision=state['revision']);layer=next(n['id'] for n in state['nodes'] if n['id'] not in old and n['kind']==3)
  before=pixels('before');rev=state['revision']
  state=ok('command',type='draw_stroke',layer_id=layer,points=[{'x':8,'y':20.5},{'x':55,'y':20.5}],width=.25,color='#000000',expected_revision=rev)
  after=pixels('after');assert after!=before
  assert after[(20*64+30)*4:(20*64+30)*4+4]==bytes([0,0,0,64])
  assert 'error' in rpc('command',type='draw_stroke',layer_id=layer,points=[{'x':1,'y':1}],width=.5,color='#000000',expected_revision=rev)
  state=ok('undo',expected_revision=state['revision']);assert pixels('undo')==before
  state=ok('redo',expected_revision=state['revision']);assert pixels('redo')==after
  state=ok('save_copy',path=str(root/'fine.draw'),expected_revision=state['revision']);ok('close',expected_revision=state['revision']);state=ok('open',path=str(root/'fine.draw'));assert pixels('reopened')==after
  assert state['strokes'][-1]['tool']=='fineliner-aa'
  ok('export',path=str(root/'fine.ora'),format='ora');inspect_ora(root/'fine.ora',after,(64,64))
  assert 'error' in rpc('command',type='draw_stroke',layer_id=layer,points=[{'x':10,'y':40,'pressure':2}],width=1,color='#000000',expected_revision=state['revision'])
  state=ok('command',type='draw_stroke',layer_id=layer,points=[{'x':8,'y':40.5,'pressure':1},{'x':55,'y':40.5,'pressure':0}],width=1,color='#000000',expected_revision=state['revision'])
  tapered=pixels('pressure');assert tapered[(40*64+10)*4+3]>200 and 0<tapered[(40*64+50)*4+3]<50
  revision=state['revision']
  assert 'error' in rpc('command',type='update_layer',node_id=layer,name='',opacity=.5,expected_revision=revision)
  state=ok('command',type='update_layer',node_id=layer,name='Ink details',opacity=.5,expected_revision=revision)
  node=next(n for n in state['nodes'] if n['id']==layer);assert node['name']=='Ink details' and node['opacity']==.5
  dimmed=pixels('opacity');assert dimmed[(20*64+30)*4+3]==32
  state=ok('undo',expected_revision=state['revision']);node=next(n for n in state['nodes'] if n['id']==layer);assert node['name']=='Color fill' and node['opacity']==1
  assert pixels('properties-undo')==tapered
  state=ok('command',type='draw_stroke',brush='dry_ink',layer_id=layer,points=[{'x':8,'y':55,'pressure':.4},{'x':55,'y':55,'pressure':1}],width=12,color='#222222',expected_revision=state['revision'])
  assert state['strokes'][-1]['tool']=='dry-ink';dry=pixels('dry-ink');assert dry!=tapered
  assert 'error' in rpc('save',expected_revision=state['revision'])
  state=ok('save_copy',path=str(root/'dry.draw'),expected_revision=state['revision']);ok('close',expected_revision=state['revision']);state=ok('open',path=str(root/'dry.draw'));assert pixels('dry-reopened')==dry
  ok('export',path=str(root/'dry.ora'),format='ora');inspect_ora(root/'dry.ora',dry,(64,64))
  print('Native fine/dry brush, pressure, layer properties, undo/redo, version-safe save/reopen and ORA passed')
 finally:
  proc.stdin.close();proc.wait(timeout=10)
