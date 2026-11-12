"""Thin single-worker transport. All painting state and history live in C++."""
from contextlib import contextmanager, asynccontextmanager
import fcntl
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
from threading import Lock
import time
from uuid import UUID, uuid4
from fastapi import APIRouter, Depends, File, HTTPException, UploadFile
from fastapi.responses import Response, FileResponse
from starlette.background import BackgroundTask


def fail(status, code, message):
    raise HTTPException(status, {'code':code,'message':message})


class Projects:
    def __init__(self, root, engine):
        self.root=Path(root)/'projects'; self.root.mkdir(parents=True,exist_ok=True)
        self.engine=engine
        self.lock=Lock(); self.proc=None; self.active=None; self.sequence=0
        self.lease=None; self.log=None

    @contextmanager
    def transaction(self):
        if not self.lock.acquire(blocking=False):
            fail(409,'busy','An engineering operation is still running; retry shortly')
        try:
            yield
        finally:
            self.lock.release()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
            self.proc.stdin.close(); self.proc.stdout.close()
        self.proc=None; self.active=None
        if self.log:self.log.close();self.log=None
        if self.lease:self.lease.close();self.lease=None

    def start(self):
        if self.proc and self.proc.poll() is None:return
        self.stop()
        try:
            self.lease=(self.root/'.owner.lock').open('a+b')
            fcntl.flock(self.lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:
            self.stop();fail(409,'busy','This workspace already has an owning server; use one worker')
        try:
            self.log=(self.root/'engine.log').open('ab')
            self.proc=subprocess.Popen([self.engine,'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log,bufsize=0)
            os.set_blocking(self.proc.stdin.fileno(),False)
        except OSError:
            self.stop();fail(503,'engine_unavailable','Native engine could not start')

    def rpc(self, method, **params):
        self.sequence+=1
        request_id=str(self.sequence)
        request=dict(request_id=request_id,method=method,params=params)
        try: payload=(json.dumps(request,allow_nan=False)+'\n').encode()
        except (ValueError,TypeError):fail(422,'invalid_input','Request must contain finite JSON values')
        if len(payload)>65536:fail(413,'invalid_input','Native command exceeds 64 KiB')
        self.start()
        try:
            deadline=time.monotonic()+120
            with selectors.DefaultSelector() as writer:
                writer.register(self.proc.stdin,selectors.EVENT_WRITE)
                position=0
                while position<len(payload):
                    remaining=deadline-time.monotonic()
                    if remaining<=0 or not writer.select(remaining):raise TimeoutError()
                    try:position+=os.write(self.proc.stdin.fileno(),payload[position:])
                    except BlockingIOError:continue
            data=bytearray()
            with selectors.DefaultSelector() as selector:
                selector.register(self.proc.stdout,selectors.EVENT_READ)
                while not data.endswith(b'\n'):
                    remaining=deadline-time.monotonic()
                    if remaining<=0 or not selector.select(remaining):raise TimeoutError()
                    chunk=os.read(self.proc.stdout.fileno(),65536)
                    if not chunk:raise EOFError()
                    data.extend(chunk)
                    if len(data)>64*1024*1024:raise ValueError('Oversized engine response')
            response=json.loads(data)
            if response.get('request_id')!=request_id:raise ValueError('IPC response mismatch')
        except (OSError,ValueError,EOFError,TimeoutError):
            self.stop()
            fail(503,'engine_unavailable','Engine disconnected or timed out. Unsaved state is lost; reopen last saved project.')
        if 'error' in response:
            error=response['error'];code=error['code']
            status={'stale_revision':409,'locked':423,'io_error':500,'engine_error':503}.get(code,422)
            if error['message']=='session.unsaved-changes':code='unsaved_changes';status=409
            fail(status,code,error['message'])
        return response['result']

    def project_path(self,pid):
        try:
            if str(UUID(pid))!=pid:raise ValueError()
        except ValueError:fail(404,'not_found','Project not found')
        return self.root/pid/'project.draw'

    def state(self, value):
        return dict(value,id=self.active)

    def save_copy(self, expected_revision):
        pid=str(uuid4())
        path=self.project_path(pid)
        path.parent.mkdir()
        # Keep a possibly written copy on an uncertain IPC failure. Never delete
        # an artifact just because the response was lost.
        result=self.rpc('save_copy',path=str(path),expected_revision=expected_revision)
        self.active=pid
        return self.state(result)

    def prepare_switch(self):
        self.start()
        if self.active:
            current=self.rpc('state')
            if current['dirty']:fail(409,'unsaved_changes','Save or explicitly close the current project before switching')
            return {'expected_revision':current['revision']}
        return {}

    def select(self,pid):
        path=self.project_path(pid)
        self.start()
        if self.active!=pid:
            if not path.is_file():fail(404,'not_found','No saved project exists at this ID')
            params=self.prepare_switch()
            self.rpc('open',path=str(path),**params);self.active=pid
        return self.state(self.rpc('state'))

    def import_file(self,source):
        params=self.prepare_switch()
        pid=str(uuid4());folder=self.root/pid;folder.mkdir()
        try:
            shutil.copyfile(source,folder/'project.draw')
            state=self.rpc('open',path=str(folder/'project.draw'),**params);self.active=pid
            return self.state(state)
        except Exception:
            shutil.rmtree(folder,ignore_errors=True)
            raise

    def render(self,pid,format,event_limit=None):
        self.select(pid)
        tmp=tempfile.TemporaryDirectory(dir=self.root)
        try:
            path=Path(tmp.name)/('preview.'+format)
            params=dict(path=str(path),format=format)
            if event_limit is not None:params['event_limit']=event_limit
            result=self.rpc('export',**params)
            return FileResponse(path,media_type='image/png' if format=='png' else 'image/openraster',background=BackgroundTask(tmp.cleanup),
                            headers={'Cache-Control':'no-store','X-Drawborad-Revision':result['revision'],'X-Drawborad-Source-Asset-Reads':result['source_asset_reads'],
                                     'X-Drawborad-Export-Warnings':json.dumps(result.get('warnings',[]),ensure_ascii=True)})
        except BaseException:
            tmp.cleanup();raise


def install_projects(app,config,authorized):
    engine=os.getenv('DRAWBORAD_NATIVE_ENGINE','DrawboradHeadless')
    projects=Projects(config.root,engine)
    router=APIRouter(prefix='/api/v1/projects',dependencies=[Depends(authorized)])
    previous_lifespan=app.router.lifespan_context
    @asynccontextmanager
    async def lifespan(app):
        async with previous_lifespan(app):
            try:yield
            finally:projects.stop()
    app.router.lifespan_context=lifespan

    @router.post('',status_code=201)
    def create(body:dict):
        with projects.transaction():
            params=projects.prepare_switch()
            state=projects.rpc('create',width=body.get('width','512'),height=body.get('height','512'),**params)
            projects.active=str(uuid4());projects.project_path(projects.active).parent.mkdir()
            return projects.state(state)

    @router.post('/import',status_code=201)
    def import_project(project:UploadFile=File(...)):
        with projects.transaction():
            with tempfile.NamedTemporaryFile(dir=projects.root) as tmp:
                total=0
                while chunk:=project.file.read(1024*1024):
                    total+=len(chunk)
                    if total>256*1024*1024:fail(413,'invalid_input','Choose a .draw file up to 256 MiB')
                    tmp.write(chunk)
                if not total:fail(413,'invalid_input','Choose a nonempty .draw file')
                tmp.flush()
                return projects.import_file(tmp.name)

    @router.post('/import-lineart',status_code=201)
    def import_lineart(lineart:UploadFile=File(...)):
        with projects.transaction():
            params=projects.prepare_switch()
            with tempfile.NamedTemporaryFile(dir=projects.root,suffix='.png') as tmp:
                total=0
                while chunk:=lineart.file.read(1024*1024):
                    if total==0 and not chunk.startswith(b'\x89PNG\r\n\x1a\n'):fail(422,'invalid_input','PNG lineart required')
                    total+=len(chunk)
                    if total>128*1024*1024:fail(413,'invalid_input','Choose a PNG lineart file up to 128 MiB')
                    tmp.write(chunk)
                if not total:fail(413,'invalid_input','Choose a nonempty PNG lineart file')
                tmp.flush()
                state=projects.rpc('import_lineart',path=tmp.name,**params)
            projects.active=str(uuid4());projects.project_path(projects.active).parent.mkdir()
            return projects.state(state)

    @router.get('/{pid}')
    def state(pid:str):
        with projects.transaction():return projects.select(pid)

    @router.post('/{pid}/commands')
    def command(pid:str,body:dict):
        with projects.transaction():
            projects.select(pid)
            fields={'recolor_stroke':{'stroke_id','color'},
                    'set_properties':{'node_id','visible','locked'},
                    'add_fill_layer':set(),
                    'bucket_fill':{'layer_id','x','y','tolerance','color'},
                    'draw_stroke':{'layer_id','points','width','color'}}
            kind=body.get('type')
            if not isinstance(kind,str) or kind not in fields:fail(422,'invalid_input','Unknown command type')
            allowed={'expected_revision','type'}|fields[kind]
            if set(body)-allowed:fail(422,'invalid_input','Unknown command fields')
            return projects.state(projects.rpc('command',**body))

    def operation(pid,method,body):
        with projects.transaction():
            projects.select(pid)
            params={'expected_revision':body.get('expected_revision')}
            if method=='save':params['path']=str(projects.project_path(pid))
            if method=='close':params['discard']=body.get('discard',False)
            result=projects.rpc(method,**params)
            if method=='close':projects.active=None;return result
            return projects.state(result)

    @router.post('/{pid}/undo')
    def undo(pid:str,body:dict):return operation(pid,'undo',body)
    @router.post('/{pid}/redo')
    def redo(pid:str,body:dict):return operation(pid,'redo',body)
    @router.post('/{pid}/save')
    def save(pid:str,body:dict):return operation(pid,'save',body)
    @router.post('/{pid}/save-copy',status_code=201)
    def save_copy(pid:str,body:dict):
        with projects.transaction():
            projects.select(pid)
            if set(body)-{'expected_revision'}:fail(422,'invalid_input','Unknown save-copy fields')
            return projects.save_copy(body.get('expected_revision'))
    @router.post('/{pid}/close')
    def close(pid:str,body:dict):return operation(pid,'close',body)
    @router.get('/{pid}/preview.png')
    def preview(pid:str,event_limit:str|None=None):
        with projects.transaction():return projects.render(pid,'png',event_limit)
    @router.get('/{pid}/export.ora')
    def ora(pid:str):
        with projects.transaction():return projects.render(pid,'ora')
    @router.get('/{pid}/download')
    def download(pid:str):
        with projects.transaction():
            state=projects.select(pid)
            if state['dirty']:fail(409,'unsaved_changes','Save the project before downloading')
            # Snapshot while holding the transaction; later saves cannot mutate
            # a file already being streamed to the browser.
            tmp=tempfile.TemporaryDirectory(dir=projects.root)
            try:
                snapshot=Path(tmp.name)/'project.draw'
                shutil.copyfile(projects.project_path(pid),snapshot)
                return FileResponse(snapshot,media_type='application/octet-stream',background=BackgroundTask(tmp.cleanup),
                                    headers={'Content-Disposition':'attachment; filename="project.draw"','Cache-Control':'no-store'})
            except BaseException:
                tmp.cleanup();raise

    app.include_router(router)
    return projects
