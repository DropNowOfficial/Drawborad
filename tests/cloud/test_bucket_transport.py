import sys
import tempfile
import unittest
from pathlib import Path
from uuid import uuid4
from fastapi import FastAPI, HTTPException
from fastapi.testclient import TestClient
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud.projects import Projects, install_projects

class SaveCopyTransport(unittest.TestCase):
    def test_success_switches_only_after_native_save_copy(self):
        with tempfile.TemporaryDirectory() as root:
            p=Projects(root,'not-executed')
            old=str(uuid4());p.active=old
            calls=[]
            def rpc(method,**params):
                self.assertEqual(p.active,old)
                calls.append((method,params))
                Path(params['path']).write_bytes(b'native-test-placeholder')
                return {'revision':'5','dirty':False}
            p.rpc=rpc
            result=p.save_copy('4')
            self.assertNotEqual(result['id'],old)
            self.assertEqual(p.active,result['id'])
            self.assertEqual(calls[0][0],'save_copy')
            self.assertEqual(calls[0][1]['expected_revision'],'4')
            self.assertEqual(Path(calls[0][1]['path']),p.project_path(result['id']))
    def test_rejection_keeps_active_project_and_old_bytes(self):
        with tempfile.TemporaryDirectory() as root:
            p=Projects(root,'not-executed');old=str(uuid4());p.active=old
            oldpath=p.project_path(old);oldpath.parent.mkdir();oldpath.write_bytes(b'old')
            def rpc(*args,**params):raise HTTPException(409,'stale')
            p.rpc=rpc
            with self.assertRaises(HTTPException):p.save_copy('3')
            self.assertEqual(p.active,old)
            self.assertEqual(oldpath.read_bytes(),b'old')
    def test_uncertain_response_does_not_delete_written_copy(self):
        with tempfile.TemporaryDirectory() as root:
            p=Projects(root,'not-executed');p.active=str(uuid4());written=[]
            def rpc(*args,**params):
                path=Path(params['path']);path.write_bytes(b'possibly-committed');written.append(path)
                raise HTTPException(503,'lost response')
            p.rpc=rpc
            with self.assertRaises(HTTPException):p.save_copy('4')
            self.assertEqual(written[0].read_bytes(),b'possibly-committed')

class CommandRoutes(unittest.TestCase):
    def test_add_fill_layer_accepts_only_revision_and_type(self):
        with tempfile.TemporaryDirectory() as root:
            app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
            pid=str(uuid4());p.active=pid;calls=[]
            p.select=lambda value:p.state({'revision':'4','dirty':True})
            p.rpc=lambda method,**params:(calls.append((method,params)) or {'revision':'5','dirty':True})
            client=TestClient(app)
            body={'expected_revision':'4','type':'add_fill_layer'}
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json=body).status_code,200)
            self.assertEqual(calls[-1],('command',body))
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json={**body,'layer_id':'arbitrary'}).status_code,422)
            self.assertEqual(len(calls),1)
    def test_bucket_payload_and_save_copy_route(self):
        with tempfile.TemporaryDirectory() as root:
            app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
            pid=str(uuid4());p.active=pid;calls=[]
            p.select=lambda value:p.state({'revision':'4','dirty':True})
            p.rpc=lambda method,**params:(calls.append((method,params)) or {'revision':'5','dirty':False})
            client=TestClient(app)
            body={'expected_revision':'4','type':'bucket_fill','layer_id':'abc','x':1,'y':2,'tolerance':3,'color':'#aabbcc'}
            result=client.post('/api/v1/projects/'+pid+'/commands',json=body)
            self.assertEqual(result.status_code,200)
            self.assertEqual(calls[-1],('command',body))
            copied=client.post('/api/v1/projects/'+pid+'/save-copy',json={'expected_revision':'5'})
            self.assertEqual(copied.status_code,201)
            self.assertNotEqual(copied.json()['id'],pid)
            self.assertEqual(calls[-1][0],'save_copy')
    def test_opt_in_under_ink_is_forwarded_only_for_bucket(self):
        with tempfile.TemporaryDirectory() as root:
            app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
            pid=str(uuid4());p.active=pid;calls=[]
            p.select=lambda value:p.state({'revision':'4','dirty':True})
            p.rpc=lambda method,**params:(calls.append((method,params)) or {'revision':'5','dirty':True})
            client=TestClient(app)
            body={'expected_revision':'4','type':'bucket_fill','layer_id':'abc','x':1,'y':2,'tolerance':0,'color':'#aabbcc','under_ink':2}
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json=body).status_code,200)
            self.assertEqual(calls[-1],('command',body))
            bad={'expected_revision':'4','type':'add_fill_layer','under_ink':2}
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json=bad).status_code,422)
            self.assertEqual(len(calls),1)
    def test_pen_payload_is_forwarded_without_adding_paths(self):
        with tempfile.TemporaryDirectory() as root:
            app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
            pid=str(uuid4());p.active=pid;calls=[]
            p.select=lambda value:p.state({'revision':'4','dirty':True})
            p.rpc=lambda method,**params:(calls.append((method,params)) or {'revision':'5','dirty':True})
            body={'expected_revision':'4','type':'draw_stroke','layer_id':'abc','points':[{'x':1.25,'y':2}],'width':.25,'color':'#101020'}
            client=TestClient(app)
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json=body).status_code,200)
            self.assertEqual(calls[-1],('command',body))
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json={**body,'path':'/arbitrary'}).status_code,422)
            self.assertEqual(len(calls),1)
    def test_unknown_or_cross_command_fields_are_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            app=FastAPI();p=install_projects(app,SimpleNamespace(root=root),lambda:None)
            pid=str(uuid4());p.active=pid;calls=[]
            p.select=lambda value:p.state({'revision':'4','dirty':True})
            p.rpc=lambda method,**params:(calls.append((method,params)) or {'revision':'5','dirty':True})
            client=TestClient(app)
            for body in [
                {'type':'bucket_fill','path':'/untrusted','expected_revision':'4'},
                {'type':'recolor_stroke','x':0,'expected_revision':'4'},
            ]:
                self.assertEqual(client.post('/api/v1/projects/'+pid+'/commands',json=body).status_code,422)
            self.assertEqual(client.post('/api/v1/projects/'+pid+'/save-copy',json={'path':'/untrusted'}).status_code,422)
            self.assertEqual(calls,[])
if __name__=='__main__':unittest.main()
