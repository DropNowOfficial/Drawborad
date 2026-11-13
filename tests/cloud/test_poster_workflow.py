"""Real 3300x5100 native workflow; no scaled fixture or substitute renderer."""
import hashlib
import io
import os
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

import pytest
from PIL import Image
from fastapi.testclient import TestClient
from cloud.app import Config, create_app


def test_poster_draw_correct_fill_and_recover(tmp_path, monkeypatch):
    engine = os.getenv('DRAWBORAD_TEST_ENGINE')
    if not engine:
        pytest.skip('Supply real native engine')
    monkeypatch.setenv('DRAWBORAD_NATIVE_ENGINE', str(Path(engine).resolve()))
    config = Config(tmp_path, None, Path('missing'), 'cpu', 'python', '')
    with TestClient(create_app(config)) as client:
        r = client.post('/api/v1/projects', json={'width': '3300', 'height': '5100'})
        assert r.status_code == 201, r.text
        state = r.json(); prefix = '/api/v1/projects/' + state['id']
        def command(**body):
            nonlocal state
            r = client.post(prefix+'/commands', json={'expected_revision':state['revision'], **body})
            assert r.status_code == 200, r.text
            state = r.json()
        def image():
            r = client.get(prefix+'/preview.png'); assert r.status_code == 200, r.text
            im = Image.open(io.BytesIO(r.content)).convert('RGBA')
            assert im.size == (3300,5100)
            return im
        def digest():
            return hashlib.sha256(image().tobytes()).hexdigest()
        command(type='add_fill_layer')
        ink = next(n['id'] for n in state['nodes'] if n['kind']==3)
        # Closed actual native stroke at full-document coordinates.
        command(type='draw_stroke', layer_id=ink, brush='smooth_ink', width=8, color='#000000',
                points=[{'x':x,'y':y} for x,y in [(1300,2200),(1900,2200),(1900,2800),(1300,2800),(1300,2200)]])
        outline = digest(); exterior = image().getpixel((100,100)); boundary = image().getpixel((1600,2200))
        command(type='add_fill_layer')
        fill = next(n['id'] for n in state['nodes'] if n['kind']==3 and n['id']!=ink)
        command(type='bucket_fill', layer_id=fill, x=1500, y=2500, tolerance=0, color='#c020b0')
        assert state['strokes'][-1]['tool']=='bucket' and state['strokes'][-1]['layer_id']==fill
        filled = digest(); assert filled != outline
        command(type='set_properties',node_id=fill,visible=False);assert digest()==outline
        command(type='set_properties',node_id=fill,visible=True);assert digest()==filled
        im = image(); assert im.getpixel((1500,2500))[:3] == (192,32,176)
        assert im.getpixel((100,100)) == exterior
        assert im.getpixel((1600,2200)) == boundary
        # Recolor on the fill layer leaves the ink and exterior unchanged.
        command(type='recolor_layer', layer_id=fill, color='#2050b0')
        im = image(); assert im.getpixel((1500,2500))[:3] == (32,80,176)
        assert im.getpixel((100,100)) == exterior
        assert im.getpixel((1600,2200)) == boundary
        before_correction = digest()
        r=client.post(prefix+'/save',json={'expected_revision':state['revision']});assert r.status_code==200,r.text;state=r.json()
        original=client.get(prefix+'/download').content
        command(type='draw_stroke',layer_id=ink,brush='smooth_ink',width=4,color='#000000',
                points=[{'x':1400,'y':2400},{'x':1800,'y':2450}])
        assert digest()!=before_correction
        r=client.post(prefix+'/undo',json={'expected_revision':state['revision']});assert r.status_code==200;state=r.json()
        assert digest()==before_correction
        command(type='draw_stroke',layer_id=ink,brush='smooth_ink',width=4,color='#000000',
                points=[{'x':1400,'y':2400},{'x':1600,'y':2380},{'x':1800,'y':2400}])
        final = digest(); strokes=state['strokes']
        assert final!=before_correction and image().getpixel((1600,2380))==(0,0,0,255)
        assert state['dirty']
        # Recovery is independent, and restores original-resolution content.
        r=client.post(prefix+'/checkpoint',json={'expected_revision':state['revision'],'expected_document_id':state['document_id']});assert r.status_code==200,r.text
        recovery=client.get(prefix+'/recovery-download').content
        assert recovery!=original
        assert (tmp_path/'projects'/state['id']/'project.draw').read_bytes()==original
        ora=client.get(prefix+'/export.ora');assert ora.status_code==200,ora.text
        with zipfile.ZipFile(io.BytesIO(ora.content)) as archive:
            stack=ET.fromstring(archive.read('stack.xml'))
            assert (int(stack.attrib['w']),int(stack.attrib['h']))==(3300,5100)
            layers=stack.findall('.//layer');assert len(layers)==2
            independent=[Image.open(io.BytesIO(archive.read(layer.attrib['src']))).convert('RGBA') for layer in layers]
            assert all(layer.getchannel('A').getbbox() for layer in independent)
            assert any(layer.getpixel((1500,2500))==(32,80,176,255) for layer in independent)
            assert any(layer.getpixel((1600,2200))==(0,0,0,255) for layer in independent)
            merged=Image.open(io.BytesIO(archive.read('mergedimage.png'))).convert('RGBA')
            assert hashlib.sha256(merged.tobytes()).hexdigest()==final
    with TestClient(create_app(config)) as restarted:
        r=restarted.post('/api/v1/projects/import',files={'project':('recovery.draw',recovery)});assert r.status_code==201,r.text
        restored=r.json();assert restored['strokes']==strokes
        pixels=Image.open(io.BytesIO(restarted.get('/api/v1/projects/'+restored['id']+'/preview.png').content)).convert('RGBA')
        assert pixels.size==(3300,5100) and hashlib.sha256(pixels.tobytes()).hexdigest()==final
        assert restarted.get(prefix+'/download').content==original
