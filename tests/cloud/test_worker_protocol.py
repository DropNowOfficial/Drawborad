"""Model boundary tests: only the external SAM3 process is simulated."""
from __future__ import annotations
from fastapi.testclient import TestClient
from PIL import Image
import io
import sys
import time
from pathlib import Path

import cloud.app as runtime


def test_subprocess_job_rejects_unrelated_source_in_manifest(tmp_path, monkeypatch):
    """The returned manifest must refer to the exact submitted image bytes."""
    script=tmp_path/'external_processor.py'
    script.write_text('''
import argparse
from pathlib import Path
import sys, os
sys.path.insert(0, os.getcwd())
from cloud.demo import make_demo
p=argparse.ArgumentParser()
for name in ('input','output','checkpoint','device'):p.add_argument('--'+name)
p.add_argument('--prompt',action='append')
a=p.parse_args()
make_demo(Path(a.output))
import json
manifest=Path(a.output)/"manifest.json"
data=json.loads(manifest.read_text());data["backend"]="sam3-image";manifest.write_text(json.dumps(data))
''')
    ckpt=tmp_path/'fake-checkpoint.pt';ckpt.write_bytes(b'not a model, test only')
    config=runtime.Config(tmp_path/'jobs',ckpt,script,'cpu',sys.executable,'')
    monkeypatch.setattr(runtime,'_ready',lambda c:True)
    image=Image.new('RGB',(8,6),'#cdede2');b=io.BytesIO();image.save(b,format='PNG')
    with TestClient(runtime.create_app(config)) as client:
        r=client.post('/api/v1/jobs',files={'image':('source.png',io.BytesIO(b.getvalue()),'image/png')},data={'prompts':'cup'})
        assert r.status_code==202,r.text
        jid=r.json()['id']
        deadline=time.monotonic()+6
        while time.monotonic()<deadline:
            data=client.get('/api/v1/jobs/'+jid).json()
            if data['state'] in ('succeeded','failed'):break
            time.sleep(.06)
        assert data['state']=='failed',data
        assert 'source' in data['error'].lower() or 'mismatch' in data['error'].lower()


def test_one_gpu_job_at_a_time_to_avoid_cloud_oom(tmp_path, monkeypatch):
    script=tmp_path/'slow_worker.py'
    script.write_text('import time; time.sleep(1.2)')
    checkpoint=tmp_path/'fake.pt';checkpoint.write_bytes(b'test stub')
    config=runtime.Config(tmp_path/'jobs',checkpoint,script,'cpu',sys.executable,'')
    monkeypatch.setattr(runtime,'_ready',lambda c:True)
    image=Image.new('RGB',(8,6),'#333333');b=io.BytesIO();image.save(b,format='PNG')
    with TestClient(runtime.create_app(config)) as c:
        def upload():
            return c.post('/api/v1/jobs',files={'image':('source.png',io.BytesIO(b.getvalue()),'image/png')},data={'prompts':'cup'})
        first=upload();assert first.status_code==202
        second=upload()
        assert second.status_code==429,second.text
        assert c.delete('/api/v1/jobs/'+first.json()['id']).status_code==200