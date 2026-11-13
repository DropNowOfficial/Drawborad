import hashlib,os
from pathlib import Path
import pytest
from fastapi.testclient import TestClient
from cloud.app import Config,create_app

def test_recovery_survives_restart_and_format_upgrade(tmp_path,monkeypatch):
 engine=os.getenv('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply native engine')
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 cfg=Config(tmp_path,None,Path('missing'),'cpu','python','')
 with TestClient(create_app(cfg)) as c:
  s=c.post('/api/v1/projects',json={'width':'64','height':'64'}).json();p='/api/v1/projects/'+s['id'];pid=s['id']
  def checkpoint(s):return c.post(p+'/checkpoint',json={'expected_revision':s['revision'],'expected_document_id':s['document_id']})
  s=c.post(p+'/commands',json={'type':'add_fill_layer','expected_revision':s['revision']}).json();layer=next(n['id']for n in s['nodes']if n['kind']==3)
  assert checkpoint(s).status_code==200
  assert c.get(p).json()==s,'checkpoint must not mark saved or change history/state'
  assert not (tmp_path/'projects'/pid/'project.draw').exists(),'new unsaved canvas remains separate'
  s=c.post(p+'/save',json={'expected_revision':s['revision']}).json();original=(tmp_path/'projects'/pid/'project.draw').read_bytes()
  s=c.post(p+'/commands',json={'type':'draw_stroke','layer_id':layer,'brush':'smooth_ink','width':16,'color':'#330066','points':[{'x':10,'y':32},{'x':50,'y':32}],'expected_revision':s['revision']}).json()
  assert s['dirty'];assert checkpoint(s).status_code==200,'format change must not overwrite prior-generation snapshot'
  after=c.get(p+'/preview.png').content;assert c.get(p).json()==s
  stale=c.post(p+'/checkpoint',json={'expected_revision':'0','expected_document_id':s['document_id']});assert stale.status_code==409
  assert (tmp_path/'projects'/pid/'project.draw').read_bytes()==original
  info=c.get(p+'/recovery').json();assert len(info['checkpoints'])==2
  same=checkpoint(s);assert same.json()=={k:v for k,v in info['checkpoints'][0].items() if k!='available'}
  before=s;s=c.post(p+'/undo',json={'expected_revision':s['revision']}).json();assert c.get(p+'/preview.png').content!=after
  s=c.post(p+'/redo',json={'expected_revision':s['revision']}).json();assert c.get(p+'/preview.png').content==after
  assert checkpoint(s).status_code==200;assert len(list((tmp_path/'projects'/pid).glob('recovery-*.draw')))==2
  recovery=c.get(p+'/recovery-download').content
 # Neither metadata nor recovery download requires a working native process.
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(tmp_path/'missing-engine'))
 with TestClient(create_app(cfg)) as c:
  assert c.get(p+'/recovery').json()['available'];assert c.get(p+'/recovery-download').content==recovery
  r=c.post(p+'/checkpoint',json={'expected_revision':s['revision'],'expected_document_id':s['document_id']});assert r.status_code==409
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(cfg)) as c:
  r=c.post('/api/v1/projects/import',files={'project':('recovery.draw',recovery)});assert r.status_code==201,r.text
  restored=r.json();assert restored['id']!=pid
  assert c.get('/api/v1/projects/'+restored['id']+'/preview.png').content==after
  assert (tmp_path/'projects'/pid/'project.draw').read_bytes()==original

def test_failed_receipt_preserves_previous_recovery(tmp_path,monkeypatch):
 engine=os.getenv('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply native engine')
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(Config(tmp_path,None,Path('missing'),'cpu','python',''))) as c:
  s=c.post('/api/v1/projects',json={'width':'32','height':'32'}).json();p='/api/v1/projects/'+s['id']
  def save(s):return c.post(p+'/checkpoint',json={'expected_revision':s['revision'],'expected_document_id':s['document_id']})
  assert save(s).status_code==200;before=c.get(p+'/recovery').json();data=c.get(p+'/recovery-download').content
  s=c.post(p+'/commands',json={'type':'add_fill_layer','expected_revision':s['revision']}).json()
  with monkeypatch.context() as m:
   def broken(*a,**kw):raise OSError('simulated receipt failure')
   m.setattr('cloud.projects.os.replace',broken)
   with pytest.raises(OSError,match='simulated receipt'):save(s)
  assert c.get(p+'/recovery').json()==before;assert c.get(p+'/recovery-download').content==data
  assert c.get(p).json()==s

def test_recovery_partial_missing_and_project_discovery(tmp_path,monkeypatch):
 import json
 engine=os.getenv('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply native engine')
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(Config(tmp_path,None,Path('missing'),'cpu','python',''))) as c:
  s=c.post('/api/v1/projects',json={'width':'32','height':'32'}).json();pid=s['id'];p='/api/v1/projects/'+pid
  def save():return c.post(p+'/checkpoint',json={'expected_revision':s['revision'],'expected_document_id':s['document_id']})
  assert save().status_code==200;s=c.post(p+'/commands',json={'type':'add_fill_layer','expected_revision':s['revision']}).json();assert save().status_code==200
  old=c.get(p+'/recovery-download?version=1');assert old.status_code==200
  folder=tmp_path/'projects'/pid;records=json.loads((folder/'recovery.json').read_text());(folder/records[0]['file']).unlink()
  info=c.get(p+'/recovery').json();assert info['available'] and not info['checkpoints'][0]['available'] and info['checkpoints'][1]['available']
  assert c.get(p+'/recovery-download').status_code==404
  assert c.get(p+'/recovery-download?version=1').content==old.content
  assert c.get(p+'/recovery-download?version=2').status_code==422
  assert save().status_code==200,'same revision must rebuild missing latest snapshot'
  assert c.get(p+'/recovery-download').status_code==200
  assert c.get(p+'/recovery-download?version=1').content==old.content,'retain the actually existing prior snapshot'
  c.post(p+'/close',json={'expected_revision':s['revision'],'discard':True})
  rows=c.get('/api/v1/projects/recovery/projects/list').json()['projects'];assert rows[0]['id']==pid
