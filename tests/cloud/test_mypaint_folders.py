"""Real pinned MyPaint + native compositing, not a fake raster fixture."""
import os
from pathlib import Path
import pytest
from fastapi.testclient import TestClient
from cloud.app import Config,create_app
from cloud import mypaint_worker

def test_real_mypaint_folder_pixels_and_recovery(tmp_path,monkeypatch):
 engine=os.getenv('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply real engine')
 available,reason=mypaint_worker.available_crop(512,512)
 if not available:pytest.skip(reason)
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(Config(tmp_path,None,Path('missing'),'cpu','python',''))) as c:
  s=c.post('/api/v1/projects',json={'width':'512','height':'512'}).json();path='/api/v1/projects/'+s['id']
  def cmd(**kw):
   r=c.post(path+'/commands',json=dict(expected_revision=s['revision'],**kw));assert r.status_code==200,r.text;return r.json()
  s=cmd(type='add_fill_layer');layer=next(n['id']for n in s['nodes']if n['kind']==3)
  s=cmd(type='bucket_fill',layer_id=layer,x=1,y=1,tolerance=0,color='#9cb0c2')
  s=c.post(path+'/save',json={'expected_revision':s['revision']}).json();base=c.get(path+'/download').content;background=c.get(path+'/preview.png').content
  def paint(folder=None):
   nonlocal s
   for i,(mode,color) in enumerate([('multiply','#924f30'),('normal','#e8c9a5'),('multiply','#2a3b61')]):
    points=[dict(x=120+j*6,y=220+i*12+(j%6)*2,pressure=.25+j*.006,time_ms=j*12)for j in range(32)]
    s=cmd(type='draw_stroke',brush='mypaint_ink',points=points,paint_mode=mode,color=color,radius_scale=.5,**({'folder_id':folder}if folder else{}))
  paint();ungrouped=c.get(path+'/preview.png').content
  assert ungrouped!=background
  c.post(path+'/close',json={'expected_revision':s['revision'],'discard':True})
  s=c.post('/api/v1/projects/import',files={'project':('base.draw',base)}).json();path='/api/v1/projects/'+s['id']
  s=cmd(type='add_mypaint_folder');folder=next(n['id']for n in s['nodes']if n.get('mypaint_folder_eligible'))
  paint(folder);grouped=c.get(path+'/preview.png').content;assert grouped==ungrouped,'neutral folder must preserve exact merged PNG'
  group=next(n for n in s['nodes']if n['id']==folder);assert len(group['children'])==3
  ink=[n for n in s['nodes']if n['parent_id']==folder];assert all(n['locked'] and n['source_kind']==0 for n in ink)
  s=c.post(path+'/undo',json={'expected_revision':s['revision']}).json();assert len(next(n['children']for n in s['nodes']if n['id']==folder))==2
  s=c.post(path+'/redo',json={'expected_revision':s['revision']}).json();assert c.get(path+'/preview.png').content==grouped
  s=cmd(type='set_stroke_mask',layer_id=ink[0]['id'],expected_document_id=s['document_id'],rect={'x':0,'y':0,'width':16,'height':16});assert c.get(path+'/preview.png').content!=grouped
  s=cmd(type='toggle_stroke_mask',layer_id=ink[0]['id'],expected_document_id=s['document_id'],enabled=False);assert c.get(path+'/preview.png').content==grouped
  assert next(n for n in s['nodes']if n['id']==ink[0]['id'])['locked']
  s=cmd(type='set_properties',node_id=folder,visible=False);assert c.get(path+'/preview.png').content==background
  s=cmd(type='set_properties',node_id=folder,visible=True)
  r=c.post(path+'/commands',json=dict(type='update_layer',node_id=folder,opacity=.5,expected_revision=s['revision']));assert r.status_code>=400
  s=cmd(type='update_layer',node_id=folder,name='Hair ink passes')
  response=c.post(path+'/save-copy',json={'expected_revision':s['revision']});assert response.status_code==201,response.text;s=response.json();path='/api/v1/projects/'+s['id'];saved=c.get(path+'/download').content
  c.post(path+'/close',json={'expected_revision':s['revision']});s=c.post('/api/v1/projects/import',files={'project':('grouped.draw',saved)}).json();path='/api/v1/projects/'+s['id']
  assert c.get(path+'/preview.png').content==grouped
  s=cmd(type='add_mypaint_folder');old=s
  r=c.post(path+'/commands',json=dict(type='draw_stroke',brush='mypaint_ink',folder_id=folder,expected_revision=s['revision'],points=[dict(x=100,y=100,pressure=.3,time_ms=0)]));assert r.status_code==422
  assert c.get(path).json()==old
