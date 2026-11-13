import os
from pathlib import Path
import pytest
from fastapi.testclient import TestClient
from cloud.app import Config,create_app

def test_native_order_api(tmp_path,monkeypatch):
 engine=os.getenv('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply real engine')
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(Config(tmp_path,None,Path('missing'),'cpu','python',''))) as c:
  s=c.post('/api/v1/projects',json={'width':'64','height':'64'}).json();path='/api/v1/projects/'+s['id']
  for _ in range(2):s=c.post(path+'/commands',json={'type':'add_fill_layer','expected_revision':s['revision']}).json()
  root=next(n for n in s['nodes']if n['kind']==0);layer=root['children'][0];old=s
  r=c.post(path+'/commands',json={'type':'reorder_layer','layer_id':layer,'direction':'up','expected_revision':s['revision']});assert r.status_code==200,r.text;s=r.json()
  assert next(n for n in s['nodes']if n['kind']==0)['children'][1]==layer
  r=c.post(path+'/commands',json={'type':'reorder_layer','layer_id':layer,'direction':'down','expected_revision':old['revision']});assert r.status_code>=400
  r=c.post(path+'/commands',json={'type':'reorder_layer','layer_id':layer,'direction':'down','expected_revision':s['revision'],'parent_id':'anything'});assert r.status_code==422
