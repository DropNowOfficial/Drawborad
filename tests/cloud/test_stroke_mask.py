"""Actual HTTP dispatch with explicit native RPC stub, not native save verification."""
import tempfile,unittest
from types import SimpleNamespace
from uuid import uuid4
from fastapi import FastAPI
from fastapi.testclient import TestClient
from cloud.projects import install_projects
class MaskAPI(unittest.TestCase):
 def test_dispatch_and_rejections(self):
  with tempfile.TemporaryDirectory() as tmp:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=tmp),lambda:None);pid=str(uuid4());p.active=pid
   node={'id':'ink','mask_supported':True,'stroke_mask':None}
   state={'width':'128','height':'64','revision':'7','document_id':'doc-a','nodes':[node],'capabilities':['cropped_stroke_mask']};calls=[]
   p.select=lambda _:state;p.state=lambda v:v
   p.rpc=lambda method,**params:(calls.append((method,params)) or state)
   body={'type':'set_stroke_mask','layer_id':'ink','expected_revision':'7','expected_document_id':'doc-a','rect':{'x':1,'y':2,'width':30,'height':20}}
   with TestClient(app) as c:
    def post(b):return c.post(f'/api/v1/projects/{pid}/commands',json=b)
    r=post(body);self.assertEqual(r.status_code,200,r.text);self.assertEqual(calls[-1],('set_stroke_mask',dict(layer_id='ink',expected_revision='7',expected_document_id='doc-a',rect=body['rect'])))
    count=len(calls)
    for bad in [dict(body,rect={'x':True,'y':0,'width':2,'height':2}),dict(body,rect={'x':127,'y':0,'width':2,'height':2}),dict(body,layer_id='group'),dict(body,expected_revision='6'),dict(body,expected_document_id='forged')]:self.assertIn(post(bad).status_code,[409,422])
    state['capabilities']=[];self.assertEqual(post(body).status_code,422);state['capabilities']=['cropped_stroke_mask'];self.assertEqual(len(calls),count)
    toggle=dict(type='toggle_stroke_mask',layer_id='ink',expected_revision='7',expected_document_id='doc-a',enabled=False)
    self.assertEqual(post(toggle).status_code,422)
    node['stroke_mask']={'enabled':True,'rect':body['rect']};self.assertEqual(post(toggle).status_code,200);self.assertEqual(calls[-1][1]['enabled'],False)
    self.assertEqual(post(dict(toggle,enabled=0)).status_code,422)
if __name__=='__main__':unittest.main()
