import sys,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from uuid import uuid4
from fastapi import FastAPI
from fastapi.testclient import TestClient
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud.projects import install_projects
def routes(router):
 for route in router.routes:
  nested=getattr(route,'original_router',None)
  if nested:yield from routes(nested)
  else:yield route
class LargeTransport(unittest.TestCase):
 def test_project_upload_streams_past_old_64mb_limit(self):
  with tempfile.TemporaryDirectory() as root:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
   class Input:
    remaining=65*1024*1024+1
    def read(self,n):
     assert n==1024*1024
     size=min(n,self.remaining);self.remaining-=size;return b'x'*size
   p.import_file=lambda path:{'size':Path(path).stat().st_size}
   route=next(r for r in routes(app) if getattr(r,'path','')=='/api/v1/projects/import')
   self.assertEqual(route.endpoint(SimpleNamespace(file=Input()))['size'],65*1024*1024+1)
   self.assertEqual(list(p.root.iterdir()),[])
 def test_export_stream_is_cleaned_after_response(self):
  with tempfile.TemporaryDirectory() as root:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None);pid=str(uuid4())
   p.select=lambda value:{'dirty':False}
   def rpc(method,**params):
    Path(params['path']).write_bytes(b'actual-stream-test')
    return {'revision':'8','source_asset_reads':'0'}
   p.rpc=rpc
   with TestClient(app) as client:
    result=client.get('/api/v1/projects/'+pid+'/export.ora')
    self.assertEqual(result.content,b'actual-stream-test');self.assertEqual(result.headers['X-Drawborad-Revision'],'8')
    self.assertEqual(list(p.root.iterdir()),[])
 def test_saved_download_is_immutable_snapshot(self):
  with tempfile.TemporaryDirectory() as root:
   app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None);pid=str(uuid4())
   path=p.project_path(pid);path.parent.mkdir();path.write_bytes(b'old-project')
   p.select=lambda value:{'dirty':False}
   route=next(r for r in routes(app) if getattr(r,'path','')=='/api/v1/projects/{pid}/download')
   response=route.endpoint(pid);path.write_bytes(b'new-project')
   self.assertEqual(Path(response.path).read_bytes(),b'old-project')
   import asyncio
   asyncio.run(response.background())
   self.assertFalse(Path(response.path).exists());self.assertEqual(path.read_bytes(),b'new-project')
if __name__=='__main__':unittest.main()
