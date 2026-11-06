"""Independent HTTP acceptance using a real photograph and actual C++ engine.
Usage: python tests/cloud/http_native_acceptance.py ENGINE INPUT_DIR OUTPUT_DIR
INPUT_DIR contains source.png and seeds.png. No TestClient or browser simulation.
"""
import io
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile
import httpx
from PIL import Image, ImageChops

engine, source, output=map(lambda p:Path(p).resolve(),sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
data=output/'server-data'
with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
url=f'http://127.0.0.1:{port}'
env=dict(os.environ,DRAWBORAD_NATIVE_ENGINE=str(engine),DRAWBORAD_CLOUD_DATA=str(data))
log=(output/'server.log').open('w')
proc=None

def start():
    global proc
    proc=subprocess.Popen([sys.executable,'-m','uvicorn','cloud.app:app','--host','127.0.0.1','--port',str(port)],env=env,stdout=log,stderr=log)
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        try:
            if httpx.get(url+'/healthz',trust_env=False).status_code==200:return
        except httpx.ConnectError:pass
        time.sleep(.1)
    raise RuntimeError('Server did not start')
def stop():
    if proc and proc.poll() is None:
        proc.terminate();proc.wait(timeout=10)
def get(path):
    r=httpx.get(url+'/api/v1'+path,timeout=120,trust_env=False);assert r.status_code==200,r.text;return r

def post(path,body=None,files=None,status=200):
    r=httpx.post(url+'/api/v1'+path,json=body,files=files,timeout=240,trust_env=False)
    assert r.status_code==status,r.text
    return r.json()

try:
    start()
    job=post('/guided',files={'image':('source.png',(source/'source.png').read_bytes()),'seeds':('seeds.png',(source/'seeds.png').read_bytes())},status=201)
    assert job['backend']=='opencv-grabcut' and not job['ai_inference']
    state=post('/projects/from-job/'+job['id'],{'accept_all':True},status=201)
    assert int(state['stroke_count'])>0
    prefix='/projects/'+state['id'];before=get(prefix+'/preview.png').content
    (output/'before.png').write_bytes(before)
    stroke=state['strokes'][-1]
    changed=post(prefix+'/commands',{'expected_revision':state['revision'],'type':'recolor_stroke','stroke_id':stroke['id'],'color':'#00ff00'})
    modified=changed['strokes'][-1]
    assert {k:v for k,v in stroke.items() if k!='color'}=={k:v for k,v in modified.items() if k!='color'}
    after=get(prefix+'/preview.png').content
    assert Image.open(io.BytesIO(before)).tobytes()!=Image.open(io.BytesIO(after)).tobytes()
    (output/'after.png').write_bytes(after)
    post(prefix+'/undo',{'expected_revision':state['revision']},status=409)
    assert get(prefix).json()['revision']==changed['revision']
    undo=post(prefix+'/undo',{'expected_revision':changed['revision']})
    assert get(prefix+'/preview.png').content==before
    redo=post(prefix+'/redo',{'expected_revision':undo['revision']})
    assert get(prefix+'/preview.png').content==after
    saved=post(prefix+'/save',{'expected_revision':redo['revision']})
    assert not saved['dirty'] and saved['saved_revision']==saved['revision']
    (output/'edited.draw').write_bytes(get(prefix+'/download').content)
    # Both reference copies are removed before closing/reopening and replay.
    for f in (data/job['id']).rglob('*.png'):
        if f.name in ('source.png','input.png'):f.rename(f.with_suffix('.removed'))
    post(prefix+'/close',{'expected_revision':saved['revision']})
    assert get(prefix).json()['strokes']==saved['strokes']
    reopened=get(prefix+'/preview.png');assert reopened.content==after
    assert reopened.headers['x-drawborad-source-asset-reads']=='0'
    for name,count in [('zero',0),('middle',int(saved['stroke_count'])//2),('final',int(saved['stroke_count']))]:
        frame=get(prefix+'/preview.png?event_limit='+str(count))
        assert frame.headers['x-drawborad-source-asset-reads']=='0'
        (output/(name+'.png')).write_bytes(frame.content)
    archive=get(prefix+'/export.ora').content;(output/'painted.ora').write_bytes(archive)
    final=Image.open(io.BytesIO(after)).convert('RGBA')
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        stack=ET.fromstring(z.read('stack.xml'));composite=Image.new('RGBA',final.size)
        for layer in reversed(list(stack.find('stack'))):
            if layer.attrib.get('visibility')=='hidden':continue
            pixels=Image.open(io.BytesIO(z.read(layer.attrib['src']))).convert('RGBA')
            opacity=float(layer.attrib['opacity']);pixels.putalpha(pixels.getchannel('A').point(lambda v:round(v*opacity)))
            composite=Image.alpha_composite(composite,pixels)
        max_error=max(v[1] for v in ImageChops.difference(composite,final).getextrema())
        assert max_error<=1
    # A subsequent unsaved edit must not masquerade as recovered after restart.
    post(prefix+'/commands',{'expected_revision':saved['revision'],'type':'recolor_stroke','stroke_id':stroke['id'],'color':'#ff0000'})
    stop();start()
    recovered=get(prefix).json();assert recovered['revision']==saved['revision'] and not recovered['dirty']
    assert get(prefix+'/preview.png').content==after
    receipt=dict(photo='Chelsea real public-domain photograph',segmentation='Human-guided GrabCut, no SAM',
        project_id=saved['id'],stroke_id=stroke['id'],stroke_count=saved['stroke_count'],saved_revision=saved['saved_revision'],
        source_asset_reads=0,ora_composite_max_error=max_error,restart_restores_saved=True,
        http_transport='real loopback HTTP; independent httpx client',browser='NOT RUN: cloud browser policy blocked',
        operations=['guided segmentation','native reconstruction','open','recolor','render change','stale rejection','undo','redo','save','close','reopen','source removed','replay','PNG','ORA','restart'])
    (output/'acceptance.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
finally:
    stop();log.close()
