import tempfile,threading,json,unittest,importlib.util
from pathlib import Path
from PIL import Image
from cloud import mypaint_worker as w
class RadiusScale(unittest.TestCase):
 def setUp(self):
  self.state=dict(width='1024',height='1024',revision='1',document_id='a'*32,capabilities=['append_brush_crop'])
  self.body=dict(type='draw_stroke',brush='mypaint_ink',expected_revision='1',points=[dict(x=400+i*2.,y=500.,pressure=.45,time_ms=i*10.)for i in range(81)])
 def test_invalid_and_legacy(self):
  for v in [None,True,'1',.249,8.01,float('nan'),float('inf')]:
   with self.assertRaises(ValueError):w.validate_request(dict(self.body,radius_scale=v),self.state)
  with tempfile.TemporaryDirectory()as d:
   with self.assertRaises(ValueError):w.render(dict(self.body,radius_scale=2),dict(self.state,capabilities=['append_brush_layer']),d,threading.Event())
 def test_http_scale_and_legacy(self):
  ok,reason=w.available_crop(1024,1024)
  if not ok:self.skipTest(reason)
  from fastapi import FastAPI
  from fastapi.testclient import TestClient
  from types import SimpleNamespace
  from uuid import uuid4
  from cloud.projects import install_projects
  with tempfile.TemporaryDirectory() as d:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=d),lambda:None);pid=str(uuid4());p.active=pid;state=dict(self.state);calls=[];p.select=lambda _:p.state(state)
   def rpc(method,**params):
    if method=='state':return state
    calls.append(method);m=json.loads(Path(params['manifest_path']).read_text());self.assertEqual(m['worker_manifest']['radius_scale'],3);self.assertIn('runtime_guarded_proposal',m);return dict(state,revision='2')
   p.rpc=rpc
   with TestClient(app)as c:
    response=c.post(f'/api/v1/projects/{pid}/commands',json=dict(self.body,radius_scale=3));self.assertEqual(response.status_code,200,response.text);self.assertEqual(calls,['append_brush_crop'])
    state['capabilities']=['append_brush_layer'];response=c.post(f'/api/v1/projects/{pid}/commands',json=dict(self.body,radius_scale=3));self.assertEqual(response.status_code,422);self.assertEqual(len(calls),1)
 def test_actual_size_and_guard(self):
  ok,reason=w.available_crop(1024,1024)
  if not ok:self.skipTest(reason)
  with tempfile.TemporaryDirectory()as d:
   records={}
   for scale in [None,1,.25,2,3,4,8]:
    folder=Path(d)/str(scale);folder.mkdir();body=dict(self.body)
    if scale is not None:body['radius_scale']=scale
    png,m=w.render(body,self.state,folder,threading.Event());im=Image.open(png).convert('RGBA');meta=json.loads(m.read_text());bbox=im.getchannel('A').getbbox();self.assertIsNotNone(bbox);records[scale]=(im,bbox,meta)
    if scale in [None,1]:self.assertNotIn('radius_scale',meta['worker_manifest']);self.assertIn('conservative_bound',meta)
    else:self.assertEqual(meta['worker_manifest']['radius_scale'],scale);self.assertIn('radius_override',meta['worker_manifest']);self.assertIn('ln(radius_scale)',meta['worker_manifest']['width_policy']);self.assertIn('runtime_guarded_proposal',meta);self.assertTrue(meta['worker_manifest']['actual_dab_guard']['passed'])
   self.assertEqual(records[None][0].tobytes(),records[1][0].tobytes())
   widths=[records[x][1][3]-records[x][1][1]for x in [.25,1,2,3,4,8]];self.assertEqual(widths,sorted(set(widths)))
   # Full pressure and8x exceeds bbox128 allowance: reject rather than crop the ink.
   folder=Path(d)/'escape';folder.mkdir()
   with self.assertRaisesRegex(RuntimeError,'exceeds internal ROI'):
    w.render(dict(self.body,radius_scale=8,points=[dict(x=100+i*4.,y=500.,pressure=1.,time_ms=i*10.) for i in range(201)]),self.state,folder,threading.Event())
   self.assertFalse((folder/'stroke.png').exists());print('actual measured stroke heights',widths)
if __name__=='__main__':unittest.main()
