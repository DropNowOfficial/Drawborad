import os
from pathlib import Path
import pytest
from fastapi.testclient import TestClient
from cloud.app import Config,create_app

def test_native_pressure_taper_api(tmp_path,monkeypatch):
 engine=os.environ.get('DRAWBORAD_TEST_ENGINE')
 if not engine:pytest.skip('Supply real native engine')
 monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
 with TestClient(create_app(Config(tmp_path,None,Path('missing'),'cpu','python',''))) as c:
  s=c.post('/api/v1/projects',json={'width':'128','height':'128'}).json();p='/api/v1/projects/'+s['id']
  def cmd(**kw):
   r=c.post(p+'/commands',json=dict(expected_revision=s['revision'],**kw));assert r.status_code==200,r.text;return r.json()
  s=cmd(type='add_fill_layer');layer=next(n['id'] for n in s['nodes'] if n['kind']==3)
  s=cmd(type='draw_stroke',layer_id=layer,points=[{'x':20,'y':30},{'x':90,'y':60}],width=6,color='#ae5432',pressure_taper=.2)
  assert s['strokes'][-1]['geometry_count']=='4'
  r=c.post(p+'/commands',json=dict(expected_revision=s['revision'],type='draw_stroke',layer_id=layer,points=[{'x':20,'y':30},{'x':90,'y':60}],width=6,color='#ae5432',pressure_taper=1));assert r.status_code>=400
