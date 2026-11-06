"""Real uploaded photos/seeds -> guided masks -> native editable project."""
import io
import os
from pathlib import Path
import pytest
from PIL import Image, ImageDraw
from fastapi.testclient import TestClient
from cloud.app import Config,create_app

def test_guided_photo_to_native_project(tmp_path,monkeypatch):
    engine=os.environ.get('DRAWBORAD_TEST_ENGINE')
    if not engine:pytest.skip('Supply real native engine')
    monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE',str(Path(engine).resolve()))
    import sys
    config=Config(tmp_path,None,Path('missing'),'cpu',sys.executable,'')
    image=Image.new('RGB',(64,64),'navy');ImageDraw.Draw(image).rectangle((16,16,47,47),fill='orange')
    seeds=Image.new('L',(64,64),0);ImageDraw.Draw(seeds).rectangle((12,12,51,51),fill=170);ImageDraw.Draw(seeds).rectangle((22,22,41,41),fill=255)
    def png(im):
        b=io.BytesIO();im.save(b,format='PNG');return b.getvalue()
    with TestClient(create_app(config)) as client:
        r=client.post('/api/v1/guided',files={'image':('photo.png',png(image)),'seeds':('seeds.png',png(seeds))})
        assert r.status_code==201,r.text
        job=r.json();assert job['backend']=='opencv-grabcut' and not job['ai_inference']
        prefix='/api/v1/projects/from-job/'+job['id']
        assert client.post(prefix,json={}).status_code==422
        r=client.post(prefix,json={'accept_all':True})
        assert r.status_code==201,r.text
        assert int(r.json()['stroke_count'])>0 and not r.json()['dirty']
