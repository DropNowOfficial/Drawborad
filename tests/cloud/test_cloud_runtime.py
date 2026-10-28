import hashlib
import io
import json
import os
from pathlib import Path
import zipfile

import numpy as np
from fastapi.testclient import TestClient
from PIL import Image

from cloud.app import create_app
from cloud.manifest import load_manifest


def fixture_archive(*, overlapping=False, path_escape=False, backend='sam3-image'):
    image = Image.new('RGB', (8, 6), '#d3c2ac')
    b = io.BytesIO()
    image.save(b, format='PNG')
    raw = b.getvalue()
    left = np.zeros((6, 8), dtype=np.uint8)
    right = np.zeros((6, 8), dtype=np.uint8)
    left[:, :4] = 255
    right[:, 4:] = 255
    if overlapping:
        right[2, 1] = 255
    masks = [left.tobytes(), right.tobytes()]
    manifest = {'schema': 1, 'backend': backend, 'promptOrder': 'back-to-front',
                'image': 'source.png', 'sourceSha256': hashlib.sha256(raw).hexdigest(),
                'width': 8, 'height': 6, 'prompts': ['wooden table', 'cup'],
                'objects': [
                    {'name': n, 'isBackground': i == 0, 'score': 1.0 if i == 0 else .9,
                     'pixels': 24 if i == 0 else (25 if overlapping else 24),
                     'mask': f'masks/layer-{i:02d}.u8',
                     'sha256': hashlib.sha256(mask).hexdigest()}
                    for i, (n, mask) in enumerate(zip(['wooden table', 'cup'], masks))]}
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w') as z:
        z.writestr('manifest.json', json.dumps(manifest))
        z.writestr('source.png', raw)
        for i, mask in enumerate(masks):
            z.writestr(f'masks/layer-{i:02d}.u8', mask)
        if path_escape:
            z.writestr('../sneak.txt', 'no')
    return result.getvalue()


def client(tmp_path, monkeypatch, token=None):
    monkeypatch.setenv('DRAWBORAD_CLOUD_DATA', str(tmp_path))
    monkeypatch.delenv('DRAWBORAD_SAM3_CHECKPOINT', raising=False)
    if token:
        monkeypatch.setenv('DRAWBORAD_CLOUD_TOKEN', token)
    else:
        monkeypatch.delenv('DRAWBORAD_CLOUD_TOKEN', raising=False)
    return TestClient(create_app())


def test_health_reports_no_checkpoint_without_faking_model(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        assert c.get('/healthz').status_code == 200
        cap = c.get('/api/v1/capabilities').json()
        assert cap['model_ready'] is False
        assert cap['mode'] == 'cloud-agent'
        response = c.post('/api/v1/jobs', files={'image': ('source.png', io.BytesIO(b'\x89PNG' ), 'image/png')}, data={'prompts':'cup'})
        assert response.status_code == 503
        assert response.json()['detail']['code'] == 'MODEL_UNAVAILABLE'


def test_import_ora_and_object_layer_integration(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        r = c.post('/api/v1/import', files={'archive': ('layers.zip', io.BytesIO(fixture_archive()), 'application/zip')})
        assert r.status_code == 201, r.text
        item = r.json()
        assert item['state'] == 'succeeded'
        assert item['backend'] == 'sam3-image'
        jid = item['id']
        assert len(item['layers']) == 2
        assert c.get(f'/api/v1/jobs/{jid}').json()['state'] == 'succeeded'
        assert c.get('/api/v1/jobs').json()['jobs'][0]['id'] == jid
        second = c.get(f'/api/v1/jobs/{jid}/layers/1.png')
        assert second.status_code == 200
        image = Image.open(io.BytesIO(second.content)).convert('RGBA')
        assert image.getpixel((1, 2))[3] == 0
        assert image.getpixel((6, 2))[3] == 255
        preview = c.get(f'/api/v1/jobs/{jid}/preview.png')
        assert preview.status_code == 200
        assert Image.open(io.BytesIO(preview.content)).size == (8, 6)
        ora = c.get(f'/api/v1/jobs/{jid}/layers.ora')
        assert ora.status_code == 200
        with zipfile.ZipFile(io.BytesIO(ora.content)) as z:
            assert z.namelist()[0] == 'mimetype'
            assert z.read('mimetype') == b'image/openraster'
            assert b'cup' in z.read('stack.xml')
            assert len([k for k in z.namelist() if k.startswith('data/layer-')]) == 2
        assert c.get(f'/api/v1/jobs/{jid}/manifest.json').json()['schema'] == 1
        assert (tmp_path / jid / 'result' / 'manifest.json').is_file()


def test_invalid_layer_overlap_is_rejected_without_job_commit(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        r = c.post('/api/v1/import', files={'archive': ('bad.zip', io.BytesIO(fixture_archive(overlapping=True)), 'application/zip')})
        assert r.status_code == 422
        assert not [x for x in tmp_path.iterdir() if (x / 'result' / 'manifest.json').exists()]


def test_zip_traversal_rejected_without_writing_outside_workspace(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        r = c.post('/api/v1/import', files={'archive': ('bad.zip', io.BytesIO(fixture_archive(path_escape=True)), 'application/zip')})
        assert r.status_code == 422
        assert not (tmp_path.parent / 'sneak.txt').exists()


def test_bearer_auth_and_missing_ids(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch, 'a-secret-abc') as c:
        assert c.get('/healthz').status_code == 200
        assert c.get('/').status_code == 200
        assert c.get('/api/v1/jobs').status_code == 401
        h = {'Authorization': 'Bearer a-secret-abc'}
        assert c.get('/api/v1/jobs', headers=h).status_code == 200
        assert c.get('/api/v1/jobs/../../abc', headers=h).status_code in (404, 422)
        assert c.get('/api/v1/jobs/00000000-0000-4000-8000-000000000000', headers=h).status_code == 404


def test_fixture_demo_explicitly_not_ai_and_persists_after_app_restart(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        r = c.post('/api/v1/demo')
        assert r.status_code == 201, r.text
        data = r.json()
        assert data['backend'] == 'fixture-no-ai'
        assert data['ai_inference'] is False
        jid = data['id']
    with client(tmp_path, monkeypatch) as c:
        assert c.get(f'/api/v1/jobs/{jid}').json()['state'] == 'succeeded'
        assert c.get(f'/api/v1/jobs/{jid}/layers.ora').status_code == 200


def test_oversized_zip_member_is_rejected_before_decompression(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('source.png', b'0' * (33 * 1024 * 1024))
        r = c.post('/api/v1/import', files={'archive': ('large.zip', data.getvalue(), 'application/zip')})
        assert r.status_code == 422


def test_imported_sam3_label_does_not_claim_server_inference(tmp_path, monkeypatch):
    with client(tmp_path, monkeypatch) as c:
        r=c.post('/api/v1/import',files={'archive':('model.zip',io.BytesIO(fixture_archive()),'application/zip')})
        assert r.status_code==201
        assert r.json()['backend']=='sam3-image'
        assert r.json()['ai_inference'] is False
        assert r.json()['origin']=='external-import'


def test_interrupted_gpu_job_recovers_as_interrupted_after_restart(tmp_path, monkeypatch):
    jid='ed81e35c-16b3-4000-af01-9486991719c0'
    folder=tmp_path/jid
    folder.mkdir(parents=True)
    (folder/'status.json').write_text(json.dumps({'id':jid,'state':'running','backend':'sam3-image'}))
    with client(tmp_path, monkeypatch) as c:
        data=c.get(f'/api/v1/jobs/{jid}').json()
        assert data['state']=='interrupted'