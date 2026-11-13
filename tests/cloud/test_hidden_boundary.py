"""Actual HTTP bridge test, native RPC explicitly stubbed."""
import tempfile,unittest
from types import SimpleNamespace
from uuid import uuid4
from fastapi import FastAPI
from fastapi.testclient import TestClient
from cloud.projects import install_projects
class BoundaryAPI(unittest.TestCase):
 def test_contract(self):
  with tempfile.TemporaryDirectory() as tmp:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=tmp),lambda:None);pid=str(uuid4());p.active=pid;calls=[]
   state=dict(width='64',height='48',revision='7',document_id='doc',capabilities=['hidden_fill_boundary'],nodes=[dict(id='ink',stage='drawborad.lineart.rgba.v1')],hidden_boundaries=[])
   p.select=lambda _:state;p.state=lambda v:v;p.rpc=lambda method,**params:(calls.append((method,params))or state)
   b=dict(type='add_hidden_boundary',lineart_id='ink',points=[dict(x=2,y=3),dict(x=6,y=4)],radius=1.5,expected_revision='7',expected_document_id='doc')
   with TestClient(app)as c:
    def post(v):return c.post(f'/api/v1/projects/{pid}/commands',json=v)
    r=post(b);self.assertEqual(r.status_code,200,r.text);self.assertEqual(calls[-1],('add_hidden_boundary',{k:v for k,v in b.items()if k!='type'}))
    for bad in [dict(b,expected_revision='6'),dict(b,expected_document_id='other'),dict(b,radius=True),dict(b,points=[dict(x=-1,y=2),dict(x=3,y=3)]),dict(b,points=[dict(x=1,y=1)]),dict(b,lineart_id='not-ink')]:self.assertIn(post(bad).status_code,[409,422])
    state['capabilities']=[];self.assertEqual(post(b).status_code,422);state['capabilities']=['hidden_fill_boundary'];self.assertEqual(len(calls),1)
    fill=dict(type='hidden_boundary_fill',lineart_id='ink',layer_id='fill',x=2,y=3,source_luminance_threshold=120,color='#abcdef',expected_revision='7',expected_document_id='doc');self.assertEqual(post(fill).status_code,200);self.assertEqual(calls[-1][0],'hidden_boundary_fill');self.assertNotIn('tolerance',calls[-1][1]);self.assertEqual(post(dict(fill,source_luminance_threshold=True)).status_code,422)
    state['hidden_boundaries']=[dict(id='boundary',points=b['points'])];toggle=dict(type='toggle_hidden_boundary',boundary_id='boundary',enabled=False,expected_revision='7',expected_document_id='doc');self.assertEqual(post(toggle).status_code,200)
if __name__=='__main__':unittest.main()
