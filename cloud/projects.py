"""Thin single-worker transport. All painting state and history live in C++."""
from contextlib import contextmanager, asynccontextmanager
import fcntl
import json
import re
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
from threading import Lock, Event
from . import mypaint_worker
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
        self.mypaint_gate=Lock();self.mypaint_cancel=None

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
        value=dict(value,id=self.active)
        caps=list(value.get('capabilities',[]))
        ok,reason=(mypaint_worker.available_crop if 'append_brush_crop' in caps else mypaint_worker.available)(value.get('width'),value.get('height'))
        if 'append_brush_layer' not in caps and 'append_brush_crop' not in caps:ok=False;reason='Native MyPaint layer support is not available in this engine build.'
        if ok:caps.append('mypaint_ink')
        presets=['pointy_ink'] if ok else []
        if ok and 'append_brush_crop' in caps:
            try:mypaint_worker.selected_pins('dry_brush');presets.append('dry_brush')
            except (ValueError,OSError):pass
        value['mypaint_presets']=presets
        value['mypaint_radius_scale_range']=[.25,8] if ok and 'append_brush_crop' in caps else [1,1]
        value['mypaint_paint_modes']=(['multiply','normal'] if ok and 'append_brush_crop' in caps and 'append_brush_normal' in caps else ['multiply'] if ok else [])
        value['capabilities']=caps;value['mypaint_status']=reason
        return value

    def mypaint_stroke(self,body,state):
        if 'mypaint_ink' not in state.get('capabilities',[]):fail(422,'unsupported',state.get('mypaint_status','MyPaint unavailable'))
        if body.get('expected_revision')!=state.get('revision'):fail(409,'stale_revision','Stale MyPaint revision')
        preset=body.get('preset','pointy_ink')
        if preset not in state.get('mypaint_presets',['pointy_ink']):fail(422,'unsupported','Selected MyPaint preset unavailable; nothing added')
        try:scale=mypaint_worker.radius_scale(body.get('radius_scale',1))
        except ValueError as e:fail(422,'invalid_input',str(e))
        if scale!=1 and 'append_brush_crop' not in state.get('capabilities',[]):fail(422,'unsupported','Scaled MyPaint requires cropped native support')
        paint_mode=body.get('paint_mode','multiply')
        if paint_mode not in state.get('mypaint_paint_modes',['multiply']):fail(422,'unsupported','Selected MyPaint paint_mode unavailable; nothing added')
        if paint_mode=='normal' and not {'append_brush_crop','append_brush_normal'}.issubset(state.get('capabilities',[])):fail(422,'unsupported','Normal paint requires native support; nothing added')
        document_id=state.get('document_id');project_id=self.active
        if not isinstance(document_id,str) or not document_id:fail(422,'unsupported','Native document identity unavailable')
        cancel=Event()
        with self.mypaint_gate:self.mypaint_cancel=cancel
        try:
            with tempfile.TemporaryDirectory(dir=self.root) as folder:
                try:png,manifest=mypaint_worker.render(body,state,folder,cancel)
                except ValueError as e:fail(422,'invalid_input',str(e))
                except (RuntimeError,OSError) as e:fail(422,'mypaint_failed',str(e))
                with self.mypaint_gate:
                    if cancel.is_set():fail(409,'cancelled','MyPaint cancelled; nothing added')
                    self.mypaint_cancel=None # Commit starts; cancellation no longer reports success.
                current=self.rpc('state')
                if self.active!=project_id or current.get('document_id')!=document_id or current.get('revision')!=body['expected_revision']:fail(409,'stale_revision','Document changed while MyPaint rendered; nothing added')
                if paint_mode=='normal' and not {'append_brush_crop','append_brush_normal'}.issubset(current.get('capabilities',[])):fail(422,'unsupported','Normal paint support changed; nothing added')
                params=dict(path=str(png),manifest_path=str(manifest),expected_revision=body['expected_revision'],expected_document_id=document_id)
                if paint_mode=='normal':params['paint_mode']='normal'
                method='append_brush_layer'
                if 'append_brush_crop' in state.get('capabilities',[]):
                    crop=json.loads(manifest.read_text());params.update(origin_x=str(crop['origin'][0]),origin_y=str(crop['origin'][1]));method='append_brush_crop'
                return self.state(self.rpc(method,**params))
        finally:
            with self.mypaint_gate:
                if self.mypaint_cancel is cancel:self.mypaint_cancel=None

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
            current=projects.select(pid)
            if body.get('type')=='draw_stroke' and body.get('brush')=='mypaint_ink':
                return projects.mypaint_stroke(body,current)
            if body.get('type') in {'add_hidden_boundary','toggle_hidden_boundary','hidden_boundary_fill'}:
                kind=body['type']
                extras={'add_hidden_boundary':{'lineart_id','points','radius'},'toggle_hidden_boundary':{'boundary_id','enabled'},'hidden_boundary_fill':{'lineart_id','layer_id','x','y','source_luminance_threshold','color'}}
                if set(body)!=({'type','expected_revision','expected_document_id'}|extras[kind]):fail(422,'invalid_input','Missing or unknown hidden boundary fields')
                if 'hidden_fill_boundary' not in current.get('capabilities',[]):fail(422,'unsupported','Native hidden boundary support unavailable')
                if body['expected_document_id']!=current.get('document_id') or body['expected_revision']!=current.get('revision'):fail(409,'stale_document','Painting identity or revision changed')
                width,height=int(current['width']),int(current['height'])
                if kind!='toggle_hidden_boundary':
                    if not any(n.get('id')==body['lineart_id'] and n.get('stage')=='drawborad.lineart.rgba.v1' for n in current.get('nodes',[])):fail(422,'invalid_input','Choose original imported lineart')
                if kind=='add_hidden_boundary':
                    import math
                    points,radius=body['points'],body['radius']
                    if type(radius) not in (int,float) or not math.isfinite(radius) or not .5<=radius<=4:fail(422,'invalid_input','Helper radius must be 0.5 to 4 pixels')
                    if not isinstance(points,list) or not 2<=len(points)<=256:fail(422,'invalid_input','Helper requires 2 to 256 points')
                    for point in points:
                        if not isinstance(point,dict) or set(point)!={'x','y'} or any(type(v) not in (int,float) or not math.isfinite(v) for v in point.values()) or not 0<=point['x']<width or not 0<=point['y']<height:fail(422,'invalid_input','Helper coordinates must be inside canvas')
                    boundaries=current.get('hidden_boundaries',[])
                    if len(boundaries)>=32 or sum(len(b.get('points',[])) for b in boundaries)+len(points)>4096:fail(422,'invalid_input','Hidden boundary budget exceeded')
                elif kind=='toggle_hidden_boundary':
                    if type(body['enabled'])!=bool or not any(b.get('id')==body['boundary_id'] for b in current.get('hidden_boundaries',[])):fail(422,'invalid_input','Choose an existing boundary and boolean enabled')
                else:
                    if any(type(body[k])!=int for k in ('x','y','source_luminance_threshold')) or not 0<=body['x']<width or not 0<=body['y']<height or not 0<=body['source_luminance_threshold']<=255:fail(422,'invalid_input','Invalid fill seed or source luminance threshold')
                    if not isinstance(body['color'],str) or not re.fullmatch(r'#[0-9a-fA-F]{6}',body['color']):fail(422,'invalid_input','Invalid fill color')
                return projects.state(projects.rpc(kind,**{k:v for k,v in body.items() if k!='type'}))
            if body.get('type') in {'set_stroke_mask','toggle_stroke_mask'}:
                kind=body['type']
                allowed={'type','layer_id','expected_revision','expected_document_id'}|({'rect'} if kind=='set_stroke_mask' else {'enabled'})
                if set(body)!=allowed:fail(422,'invalid_input','Missing or unknown single-stroke mask fields')
                if 'cropped_stroke_mask' not in current.get('capabilities',[]):fail(422,'unsupported','Native single-stroke masks unavailable; nothing changed')
                node=next((n for n in current.get('nodes',[]) if n.get('id')==body.get('layer_id')),None)
                if not node or node.get('mask_supported') is not True:fail(422,'unsupported','Select a supported cropped MyPaint stroke, not a group')
                if body.get('expected_document_id')!=current.get('document_id'):fail(409,'stale_document','Painting identity changed; refresh before editing mask')
                if body.get('expected_revision')!=current.get('revision'):fail(409,'stale_revision','Painting changed; refresh before editing mask')
                if kind=='set_stroke_mask':
                    rect=body['rect']
                    if not isinstance(rect,dict) or set(rect)!={'x','y','width','height'} or any(type(v)!=int for v in rect.values()):fail(422,'invalid_input','Mask rectangle requires integer canvas coordinates')
                    x,y,w,h=(rect[k] for k in ('x','y','width','height'))
                    if x<0 or y<0 or w<=0 or h<=0 or x+w>int(current['width']) or y+h>int(current['height']):fail(422,'invalid_input','Mask rectangle must lie inside the canvas')
                elif type(body['enabled'])!=bool or not node.get('stroke_mask'):fail(422,'invalid_input','Toggle requires an existing mask and boolean enabled')
                params={k:v for k,v in body.items() if k!='type'}
                return projects.state(projects.rpc(kind,**params))
            fields={'recolor_layer':{'layer_id','color'},'recolor_stroke':{'stroke_id','color'},
                    'set_properties':{'node_id','visible','locked'},
                    'add_fill_layer':set(),
                    'bucket_fill':{'layer_id','x','y','tolerance','color','under_ink'},
                    'draw_stroke':{'layer_id','points','width','color','brush'},
                    'update_layer':{'node_id','name','opacity'}}
            kind=body.get('type')
            if not isinstance(kind,str) or kind not in fields:fail(422,'invalid_input','Unknown command type')
            allowed={'expected_revision','type'}|fields[kind]
            if set(body)-allowed:fail(422,'invalid_input','Unknown command fields')
            return projects.state(projects.rpc('command',**body))

    @router.post('/{pid}/cancel-brush')
    def cancel_brush(pid:str):
        projects.project_path(pid)
        with projects.mypaint_gate:
            if projects.active!=pid or projects.mypaint_cancel is None:fail(409,'not_rendering','No cancellable MyPaint render; native commit may already have started')
            projects.mypaint_cancel.set()
        return {'cancel_requested':True}

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
