"""Actual page pressure transform + real pinned worker; no artificial raster substitute."""
import json,subprocess,threading
from pathlib import Path
import pytest
from PIL import Image
from cloud import mypaint_worker as worker
@pytest.mark.parametrize('preset',['pointy_ink','dry_brush'])
@pytest.mark.parametrize('shape',['dense','two_points','same_time','short'])
def test_taper_actual_worker(tmp_path,preset,shape):
 ok,reason=worker.available_crop(512,512)
 if not ok:pytest.skip(reason)
 html=Path('cloud/static/index.html').read_text();helper=html[html.index('function nativeMyPaintTaperPoints('):html.index('function nativeInkDraft(')]
 points=[dict(x=80+i*4.,y=150.,pressure=.35,time_ms=i*12.)for i in range(81)]
 if shape=='two_points':points=[points[0],points[-1]]
 if shape=='same_time':
  for p in points:p['time_ms']=0
 if shape=='short':points=[dict(x=80+i*.1,y=150.,pressure=.35,time_ms=i*12.)for i in range(11)]
 results=[]
 for taper in [0,.2]:
  actual=json.loads(subprocess.check_output(['node','-e',helper+';console.log(JSON.stringify(nativeMyPaintTaperPoints('+json.dumps(points)+','+str(taper)+')));']))
  body=dict(type='draw_stroke',brush='mypaint_ink',expected_revision='1',preset=preset,points=actual)
  folder=tmp_path/str(taper);folder.mkdir()
  if shape=='short' and preset=='pointy_ink':
   with pytest.raises(RuntimeError,match='no visible ink'):worker.render(body,dict(width='512',height='512',revision='1',document_id='a'*32,capabilities=['append_brush_crop']),folder,threading.Event())
   assert not (folder/'crop-manifest.json').exists();continue
  png,manifest=worker.render(body,dict(width='512',height='512',revision='1',document_id='a'*32,capabilities=['append_brush_crop']),folder,threading.Event())
  m=json.loads(manifest.read_text());assert m['original_global_points']==actual
  im=Image.open(png).convert('RGBA');assert im.getchannel('A').getbbox();results.append(png.read_bytes())
 if results:assert results[0]!=results[1]
