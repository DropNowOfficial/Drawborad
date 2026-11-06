"""Project API contract; engine-unavailable must be explicit, not fake state."""
from pathlib import Path
from fastapi.testclient import TestClient
from cloud.app import Config, create_app

def test_project_api_reports_unavailable_engine(tmp_path, monkeypatch):
    monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE', str(tmp_path/'missing-engine'))
    config=Config(tmp_path, None, Path('missing-model'), 'cpu', 'python', 'secret')
    with TestClient(create_app(config)) as client:
        assert client.post('/api/v1/projects',json={}).status_code==401
        response=client.post('/api/v1/projects',headers={'Authorization':'Bearer secret'},json={})
        assert response.status_code==503
        assert response.json()['detail']['code']=='engine_unavailable'

import os
import subprocess
import pytest

def test_real_native_edit_save_reconnect_and_restart(tmp_path, monkeypatch):
    engine=os.environ.get('DRAWBORAD_TEST_ENGINE')
    if not engine:
        pytest.skip('Native executable must be explicitly supplied')
    engine=str(Path(engine).resolve())
    monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',engine)
    subprocess.run([engine,'smoke','--output',str(tmp_path/'fixture')],check=True,capture_output=True)
    config=Config(tmp_path/'data',None,Path('missing-model'),'cpu','python','')
    with TestClient(create_app(config)) as client:
        response=client.post('/api/v1/projects/import',files={'project':('source.draw',(tmp_path/'fixture/project.draw').read_bytes())})
        assert response.status_code==201,response.text
        state=response.json(); pid=state['id']; prefix='/api/v1/projects/'+pid
        original=client.get(prefix+'/preview.png').content
        event=state['strokes'][-1]
        response=client.post(prefix+'/commands',json={'expected_revision':state['revision'],'type':'recolor_stroke','stroke_id':event['id'],'color':'#00ff00'})
        assert response.status_code==200,response.text
        edited=response.json(); after=client.get(prefix+'/preview.png').content
        assert after!=original and edited['dirty']
        stale=client.post(prefix+'/undo',json={'expected_revision':state['revision']})
        assert stale.status_code==409
        assert client.get(prefix).json()['revision']==edited['revision']
        undone=client.post(prefix+'/undo',json={'expected_revision':edited['revision']}).json()
        assert client.get(prefix+'/preview.png').content==original
        redone=client.post(prefix+'/redo',json={'expected_revision':undone['revision']}).json()
        saved=client.post(prefix+'/save',json={'expected_revision':redone['revision']}).json()
        assert not saved['dirty'] and saved['saved_revision']==saved['revision']
        assert client.get(prefix+'/preview.png').content==after
        assert client.get(prefix+'/download').content
        assert client.get(prefix+'/export.ora').content[:2]==b'PK'
        assert client.get(prefix+'/preview.png?event_limit=0').content!=after
        # Unsaved changes survive client refresh but not server shutdown.
        changed=client.post(prefix+'/commands',json={'expected_revision':saved['revision'],'type':'recolor_stroke','stroke_id':event['id'],'color':'#ff0000'}).json()
        assert client.get(prefix).json()['revision']==changed['revision']
    with TestClient(create_app(config)) as restarted:
        reopened=restarted.get(prefix)
        assert reopened.status_code==200,reopened.text
        assert reopened.json()['revision']==saved['revision']
        assert not reopened.json()['dirty']
        assert restarted.get(prefix+'/preview.png').content==after
