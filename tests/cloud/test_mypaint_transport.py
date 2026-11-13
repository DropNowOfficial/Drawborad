import sys,tempfile,unittest,json
from pathlib import Path
from types import SimpleNamespace
from uuid import uuid4
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from fastapi import FastAPI
from fastapi.testclient import TestClient
from cloud.projects import install_projects,Projects
from cloud import mypaint_worker
class Transport(unittest.TestCase):
 def test_native_cap_required(self):
  with tempfile.TemporaryDirectory() as d:
   p=Projects(d,'unused');self.assertNotIn('mypaint_ink',p.state({'width':512,'height':512,'capabilities':[]})['capabilities'])
 def test_real_worker_to_native_rpc_boundary(self):
  if not mypaint_worker.available(128,128)[0]:self.skipTest('optional pinned runtime unavailable')
  with tempfile.TemporaryDirectory() as d:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=d),lambda:None);pid=str(uuid4());p.active=pid
   state={'document_id':'f'*32,'revision':'1','width':'128','height':'128','capabilities':['append_brush_layer'],'dirty':False}
   p.select=lambda _:p.state(state);calls=[]
   self.assertIn('mypaint_ink',p.state(state)['capabilities'])
   self.assertEqual(p.state(state)['width'],'128')
   def rpc(method,**params):
    if method=='state':return state
    self.assertEqual(params['expected_document_id'],'f'*32)
    # This is explicitly a boundary stub, NOT a native end-to-end acceptance.
    self.assertEqual(method,'append_brush_layer');self.assertEqual(params['expected_revision'],'1')
    self.assertTrue(Path(params['path']).is_file());doc=json.loads(Path(params['manifest_path']).read_text());self.assertEqual(doc['backend'],'mypaint-pointy-ink-raster-v1');calls.append(doc)
    return dict(state,revision='2',dirty=True)
   p.rpc=rpc
   body={'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'1','points':[{'x':30,'y':60,'pressure':.2,'time_ms':0},{'x':95,'y':60,'pressure':.3,'time_ms':100}]}
   with TestClient(app) as client:
    self.assertEqual(client.post(f'/api/v1/projects/{pid}/commands',json=dict(body,expected_revision='0')).status_code,409);self.assertEqual(calls,[])
    result=client.post(f'/api/v1/projects/{pid}/commands',json=body);self.assertEqual(result.status_code,200,result.text);self.assertEqual(result.json()['revision'],'2');self.assertEqual(len(calls),1)
    self.assertEqual(client.post(f'/api/v1/projects/{pid}/cancel-brush',json={}).status_code,409)
    def switched_rpc(method,**params):
     if method=='state':return dict(state,document_id='e'*32)
     raise AssertionError('must not append into switched document')
    p.rpc=switched_rpc
    self.assertEqual(client.post(f'/api/v1/projects/{pid}/commands',json=body).status_code,409)
if __name__=='__main__':unittest.main()
