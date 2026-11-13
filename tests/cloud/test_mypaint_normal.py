"""Real optional worker, explicit HTTP RPC-boundary stub; not native Normal acceptance."""
import json,tempfile,threading,unittest
from pathlib import Path
from types import SimpleNamespace
from uuid import uuid4
from unittest.mock import patch
from fastapi import FastAPI
from fastapi.testclient import TestClient
from cloud import mypaint_worker as w
from cloud.projects import install_projects
class NormalPaint(unittest.TestCase):
 def setUp(self):
  self.state={'width':'512','height':'512','revision':'1','document_id':'a'*32,'capabilities':['append_brush_crop','append_brush_normal']}
  self.body=dict(type='draw_stroke',brush='mypaint_ink',expected_revision='1',color='#FFFFFF',points=[dict(x=100+i*3.,y=250.,pressure=.55,time_ms=i*10.)for i in range(61)])
 def test_real_white_coverage_and_metadata(self):
  ok,reason=w.available_crop(512,512)
  if not ok:self.skipTest(reason)
  from PIL import Image
  with tempfile.TemporaryDirectory() as tmp:
   images=[]
   for mode in [None,'multiply','normal']:
    d=Path(tmp)/str(mode);d.mkdir();body=dict(self.body)
    if mode:body['paint_mode']=mode
    png,meta=w.render(body,self.state,d,threading.Event());im=Image.open(png).convert('RGBA');images.append(im.tobytes());m=json.loads(meta.read_text())['worker_manifest']
    self.assertEqual(m.get('paint_mode'), 'normal' if mode=='normal' else None)
    self.assertTrue(any(p[3]>0 for p in im.getdata()));self.assertTrue(all(p[:3]==(255,255,255)for p in im.getdata()))
   self.assertEqual(images[0],images[1]);self.assertEqual(images[1],images[2])
   # A white coverage pixel alpha a over black yields a under Normal, 0 under Multiply.
   self.assertTrue(any(p[3]>0 for p in im.getdata()))
 def test_http_modes_and_capability(self):
  ok,reason=w.available_crop(512,512)
  if not ok:self.skipTest(reason)
  with tempfile.TemporaryDirectory() as tmp:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=tmp),lambda:None);pid=str(uuid4());p.active=pid;state=dict(self.state);calls=[];p.select=lambda _:p.state(state)
   def rpc(method,**params):
    if method=='state':return state
    calls.append((method,params));self.assertEqual(method,'append_brush_crop');self.assertEqual(params.get('paint_mode'),'normal');meta=json.loads(Path(params['manifest_path']).read_text());self.assertEqual(meta['worker_manifest']['paint_mode'],'normal');return dict(state,revision='2')
   p.rpc=rpc
   with TestClient(app) as c:
    response=c.post(f'/api/v1/projects/{pid}/commands',json=dict(self.body,paint_mode='normal'));self.assertEqual(response.status_code,200,response.text);self.assertEqual(len(calls),1)
    state['capabilities']=['append_brush_crop'];response=c.post(f'/api/v1/projects/{pid}/commands',json=dict(self.body,paint_mode='normal'));self.assertEqual(response.status_code,422);self.assertEqual(len(calls),1)
    response=c.post(f'/api/v1/projects/{pid}/commands',json=dict(self.body,paint_mode='bogus'));self.assertEqual(response.status_code,422);self.assertEqual(len(calls),1)
 def test_bad_modes_and_legacy(self):
  for mode in ['Normal','',None,1,True,{}]:
   with self.assertRaises(ValueError):w.validate_request(dict(self.body,paint_mode=mode),self.state)
  with tempfile.TemporaryDirectory() as tmp:
   for caps in [[],['append_brush_layer'],['append_brush_crop'],['append_brush_normal']]:
    with self.assertRaisesRegex(ValueError,'explicit native capability'):
     w.render(dict(self.body,paint_mode='normal'),dict(self.state,capabilities=caps),tmp,threading.Event())
   self.assertEqual(list(Path(tmp).iterdir()),[],'capability rejection must precede output')
if __name__=='__main__':unittest.main()
