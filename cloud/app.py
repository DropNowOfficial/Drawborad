"""FastAPI UI + agent-neutral REST gateway for Drawborad semantic painting inputs.

CPU deployment is usable for inspecting/importing layers and OpenRaster export.
SAM 3 inference is only reported available if its licensed checkpoint, official
package, and image runner are actually present. Nothing silently uses GrabCut.
"""
from __future__ import annotations

from dataclasses import dataclass
import hmac
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from threading import Lock, Thread
import time
from typing import Optional
from uuid import UUID, uuid4
import zipfile

from fastapi import Depends, FastAPI, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, Response
from PIL import Image, ImageOps, UnidentifiedImageError

from .manifest import ManifestError, load_manifest, MAX_PIXELS, MAX_SOURCE_BYTES
from .exports import ora_bytes, png_bytes, preview_png, rgba_layer
from .demo import make_demo

API = '/api/v1'
MAX_ZIP_BYTES = 48 * 1024 * 1024
MAX_UNPACKED = 96 * 1024 * 1024
MASK_MEMBER = re.compile(r'masks/layer-\d{2}\.u8\Z')
BASE = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Config:
    root: Path
    checkpoint: Path | None
    script: Path
    device: str
    python: str
    token: str


def _settings() -> Config:
    ck = os.environ.get('DRAWBORAD_SAM3_CHECKPOINT','').strip()
    return Config(Path(os.getenv('DRAWBORAD_CLOUD_DATA',str(BASE/'.cloud-data'))).resolve(),
                  Path(ck).resolve() if ck else None,
                  Path(os.getenv('DRAWBORAD_CLOUD_SAM3_SCRIPT',str(BASE/'tools/models/sam3_semantic.py'))).resolve(),
                  os.getenv('DRAWBORAD_SAM3_DEVICE','cuda'),
                  os.getenv('DRAWBORAD_SAM3_PYTHON',sys.executable),
                  os.getenv('DRAWBORAD_CLOUD_TOKEN',''))


def _ready(config: Config) -> bool:
    return bool(config.checkpoint and config.checkpoint.is_file() and config.script.is_file()
                and importlib.util.find_spec('sam3') is not None and config.device in ('cuda','cpu'))


def _clean_id(value: str) -> str:
    try:
        return str(UUID(value)) if str(UUID(value)) == value else (_ for _ in ()).throw(ValueError())
    except (ValueError, AttributeError) as exc:
        raise HTTPException(404, 'Job not found') from exc


def _atomic_json(path: Path, value: dict) -> None:
    tmp=path.with_suffix('.tmp')
    tmp.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    tmp.replace(path)


def _status(config: Config, jid: str) -> dict:
    path=config.root/jid/'status.json'
    if not path.is_file():
        raise HTTPException(404,'Job not found')
    try:
        data=json.loads(path.read_text(encoding='utf-8'))
    except (ValueError, OSError) as exc:
        raise HTTPException(500,'Corrupted job status') from exc
    return data


def _done(config: Config, jid: str, *, state='succeeded', message='', origin='external-import') -> dict:
    folder=config.root/jid
    doc=load_manifest(folder/'result')
    (folder/'preview.png').write_bytes(preview_png(doc))
    (folder/'layers.ora').write_bytes(ora_bytes(doc))
    doc_layers=tuple({'index':idx,'name':layer.name,'score':layer.score,
                     'pixels':layer.count,'is_background':layer.is_background,
                     'png':f'{API}/jobs/{jid}/layers/{idx}.png'} for idx,layer in enumerate(doc.layers))
    data={'id':jid,'state':state,'backend':doc.manifest['backend'],
          'ai_inference':origin=='local-model' and doc.is_ai,'origin':origin,'width':doc.width,'height':doc.height,
          'layers':doc_layers,'message':message,
          'manifest_url':f'{API}/jobs/{jid}/manifest.json',
          'preview_url':f'{API}/jobs/{jid}/preview.png',
          'ora_url':f'{API}/jobs/{jid}/layers.ora'}
    _atomic_json(folder/'status.json', data)
    return data


def _extract_bundle(data: bytes, root: Path) -> None:
    try:
        archive=zipfile.ZipFile(io.BytesIO(data))
    except (zipfile.BadZipFile, OSError) as exc:
        raise ManifestError('Expected a valid ZIP archive') from exc
    with archive:
        members=archive.infolist()
        if not 2 <= len(members) <= 75:
            raise ManifestError('Unreasonable number of archive members')
        total=0
        for item in members:
            name=item.filename
            allowed=(name == 'manifest.json' or name in ('source.png','source.jpg','source.jpeg')
                     or bool(MASK_MEMBER.fullmatch(name)) or name == 'masks/')
            if not allowed or '\\' in name or item.flag_bits & 0x1 or ((item.external_attr>>16) & 0o170000) == 0o120000:
                raise ManifestError('Archive contains unsafe or unrecognized members')
            if item.file_size > 32*1024*1024 or (item.file_size and item.compress_size==0):
                raise ManifestError('Oversized or invalid archive member')
            total+=item.file_size
            if total > MAX_UNPACKED:
                raise ManifestError('Archive expands beyond import quota')
        if len({m.filename for m in members}) != len(members):
            raise ManifestError('Duplicate archive entries')
        root.mkdir(parents=True,exist_ok=False)
        for item in members:
            if item.is_dir():
                continue
            target=root/item.filename
            target.parent.mkdir(parents=True,exist_ok=True)
            with archive.open(item) as read, target.open('xb') as write:
                # The size has already been checked; do not use extractall().
                shutil.copyfileobj(read,write,1024*1024)
    load_manifest(root)


def _validate_upload(data: bytes, filename: str) -> str:
    ext=Path(filename or '').suffix.lower()
    if ext not in ('.png','.jpg','.jpeg') or not data or len(data)>MAX_SOURCE_BYTES:
        raise HTTPException(422, 'Choose a PNG/JPEG image no larger than 32 MiB')
    try:
        with Image.open(io.BytesIO(data)) as image:
            if image.format not in ('PNG','JPEG') or image.width*image.height>MAX_PIXELS or max(image.size)>4096:
                raise ValueError('Input format or dimensions not supported')
            exif=ImageOps.exif_transpose(image)
            if 'A' in exif.getbands():
                a=exif.getchannel('A').tobytes()
                if any(v not in (0,255) for v in a):
                    raise ValueError('Partial alpha is not supported by the native engine')
    except (ValueError, OSError, UnidentifiedImageError, Image.DecompressionBombError) as exc:
        raise HTTPException(422,str(exc)) from exc
    return ext


def create_app(config: Config | None = None) -> FastAPI:
    config=config or _settings()
    config.root.mkdir(parents=True,exist_ok=True)
    # This deployment is single-worker; jobs interrupted by a restarted
    # container must never remain displayed as indefinitely 'running'.
    for status_file in config.root.glob('*/status.json'):
        try:
            record=json.loads(status_file.read_text(encoding='utf-8'))
            if record.get('state') in ('queued','running'):
                record['state']='interrupted'
                record['error']='Host process restarted while inference was active'
                _atomic_json(status_file,record)
        except (ValueError,OSError):
            continue
    app=FastAPI(title='Drawborad Cloud Agent Runtime',version='0.1.0',
                description='Actual SAM3 segmentation runner + inspectable independent RGBA object layers; no checkpoint is bundled')
    lock=Lock()
    processes: dict[str,subprocess.Popen] = {}

    def authorized(request: Request):
        if config.token:
            provided=request.headers.get('authorization','')
            if not hmac.compare_digest(provided,'Bearer '+config.token):
                raise HTTPException(401,'Bearer token required')

    from .projects import install_projects
    native_projects = install_projects(app, config, authorized)

    def get_job(jid: str) -> dict:
        return _status(config,_clean_id(jid))

    @app.get('/', response_class=HTMLResponse)
    def homepage():
        return (BASE/'cloud/static/index.html').read_text(encoding='utf-8')

    @app.get('/healthz')
    def health():
        return {'status':'ok','service':'drawborad-cloud-agent','model_inference_verified':False}

    @app.get(f'{API}/capabilities',dependencies=[Depends(authorized)])
    def capabilities():
        return {'mode':'cloud-agent','model_ready':_ready(config),
                'model_checkpoint_available':bool(config.checkpoint and config.checkpoint.is_file()),
                'model_package_installed':importlib.util.find_spec('sam3') is not None,
                'runner_available':config.script.is_file(),
                'device':config.device,'native_cpp_painting':bool(shutil.which(native_projects.engine)), 'native_engine_configured':bool(shutil.which(native_projects.engine)),
                'supported_exports':['OpenRaster RGBA layers','PNG layer cutouts','SAM3 native manifest'],
                'note':'A healthy server does not imply successful SAM3 inference or native C++ brush rendering'}

    @app.get(f'{API}/jobs',dependencies=[Depends(authorized)])
    def list_jobs():
        jobs=[]
        for f in sorted(config.root.glob('*/status.json'),reverse=True):
            if len(jobs)>=60:break
            try: jobs.append(json.loads(f.read_text()))
            except (OSError,ValueError):continue
        return {'jobs':jobs}

    @app.get(f'{API}/jobs/{{jid}}',dependencies=[Depends(authorized)])
    def status(jid: str):
        return get_job(jid)

    @app.post(f'{API}/import',status_code=201,dependencies=[Depends(authorized)])
    async def import_manifest(archive: UploadFile=File(...)):
        data=await archive.read(MAX_ZIP_BYTES+1)
        if len(data)>MAX_ZIP_BYTES:
            raise HTTPException(413,'ZIP file is too large')
        jid=str(uuid4());folder=config.root/jid
        try:
            folder.mkdir()
            _extract_bundle(data,folder/'result')
            return _done(config,jid)
        except (ManifestError,ValueError,zipfile.BadZipFile,OSError) as exc:
            shutil.rmtree(folder,ignore_errors=True)
            raise HTTPException(422,str(exc)) from exc

    @app.post(f'{API}/demo',status_code=201,dependencies=[Depends(authorized)])
    def demo():
        jid=str(uuid4());folder=config.root/jid;folder.mkdir()
        try:
            make_demo(folder/'result')
            return _done(config,jid,origin='synthetic-fixture',message='SYNTHETIC FIXTURE: not SAM inference or an accuracy measurement')
        except (ValueError,OSError) as exc:
            shutil.rmtree(folder,ignore_errors=True)
            raise HTTPException(500,str(exc)) from exc

    @app.post(f'{API}/guided', status_code=201, dependencies=[Depends(authorized)])
    def guided(image: UploadFile=File(...), seeds: UploadFile=File(...)):
        # Explicit seed map, no automatic recognition or SAM claim.
        data=image.file.read(MAX_SOURCE_BYTES+1)
        ext=_validate_upload(data,image.filename or '')
        seed_data=seeds.file.read(MAX_SOURCE_BYTES+1)
        if not seed_data or len(seed_data)>MAX_SOURCE_BYTES:
            raise HTTPException(413,'Seed PNG exceeds the upload limit')
        with native_projects.transaction():
            jid=str(uuid4());folder=config.root/jid;folder.mkdir()
            try:
                source=folder/('input'+ext);source.write_bytes(data)
                seed_path=folder/'seeds.png';seed_path.write_bytes(seed_data)
                with (folder/'worker.log').open('wb') as log:
                    result=subprocess.run([sys.executable,str(BASE/'tools/segmentation/guided_grabcut.py'),
                        '--image',str(source),'--seeds',str(seed_path),'--output',str(folder/'result')],
                        stdout=log,stderr=subprocess.STDOUT,timeout=120)
                if result.returncode:
                    raise HTTPException(422,{'code':'guided_failed','message':'Guided segmentation failed; check seed labels and image dimensions'})
                return _done(config,jid,origin='human-guided',message='Human-guided GrabCut: two visible regions, not automatic multi-object recognition. Review masks before generating strokes.')
            except subprocess.TimeoutExpired:
                shutil.rmtree(folder,ignore_errors=True)
                raise HTTPException(504,'Guided segmentation timed out')
            except Exception:
                shutil.rmtree(folder,ignore_errors=True)
                raise

    @app.post(f'{API}/projects/from-job/{{jid}}', status_code=201, dependencies=[Depends(authorized)])
    def reconstruct_job(jid: str, body: dict):
        record=get_job(jid)
        if record.get('state')!='succeeded' or body.get('accept_all') is not True:
            raise HTTPException(422,'Review the masks and explicitly accept all objects first')
        planner=body.get('planner','sweep')
        if not isinstance(planner,str) or planner not in ('sweep','multiscale','artist','artist-opt'):
            raise HTTPException(422,'Invalid planner; choose sweep, multiscale, artist, or artist-opt')
        amodal=body.get('amodal_background',False)
        if type(amodal) is not bool:
            raise HTTPException(422,'amodal_background must be a boolean')
        target_size=body.get('target_size')
        if target_size is not None and (not isinstance(target_size,str) or not re.fullmatch(r'[1-9]\d{0,4}x[1-9]\d{0,4}',target_size)):
            raise HTTPException(422,'target_size must match WxH')
        is_fixture=(record.get('origin')=='synthetic-fixture' or record.get('backend')=='fixture-no-ai')
        with native_projects.transaction():
            native_projects.prepare_switch()
            import tempfile
            with tempfile.TemporaryDirectory(dir=config.root) as tmp:
                output=Path(tmp)/'painting'
                manifest_path=str(config.root/jid/'result/manifest.json')
                try:
                    with (config.root/jid/'reconstruction.log').open('wb') as log:
                        if planner=='artist-opt':
                            cmd=[sys.executable,'-m','cloud.planner.cli','--manifest',manifest_path,
                                 '--engine',native_projects.engine,'--output',str(output)]
                            if is_fixture:cmd.append('--fixture')
                            if amodal:cmd.append('--amodal-background')
                            if target_size:cmd+=['--target-size',target_size,'--target-dpi','300']
                            result=subprocess.run(cmd,cwd=str(BASE),stdout=log,stderr=subprocess.STDOUT,timeout=300)
                        else:
                            cmd=[native_projects.engine,'run','--manifest',manifest_path,
                                 '--output',str(output),'--accept-all','--planner',planner]
                            if is_fixture:cmd.append('--fixture')
                            if amodal:cmd.append('--amodal-background')
                            if target_size:cmd+=['--target-size',target_size,'--target-dpi','300']
                            result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=240)
                except subprocess.TimeoutExpired:
                    raise HTTPException(504,{'code':'reconstruction_timeout','message':'Native reconstruction timed out'})
                except OSError:
                    raise HTTPException(503,{'code':'engine_unavailable','message':'Native reconstruction could not start'})
                if result.returncode:
                    raise HTTPException(422,{'code':'reconstruction_failed','message':'Native engine rejected this input; no project was replaced'})
                return native_projects.import_file(output/'project.draw')

    def _work(jid: str, args: list[str]):
        folder=config.root/jid
        try:
            with lock:
                state=_status(config,jid)
                if state['state']!='queued':return
                state['state']='running'
                _atomic_json(folder/'status.json',state)
            with (folder/'worker.log').open('wb') as log:
                proc=subprocess.Popen(args,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,
                                      cwd=str(BASE),close_fds=True)
                with lock:processes[jid]=proc
                try:
                    code=proc.wait(timeout=1800)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait();raise RuntimeError('SAM3 process timed out')
                finally:
                    with lock:processes.pop(jid,None)
            if _status(config,jid)['state']=='cancelled':return
            if code!=0:
                raise RuntimeError('SAM3 inference exited '+str(code)+'; see worker diagnostic')
            # Never accept a model script that quietly emits some unrelated
            # source image or a synthetic fixture under a completed real job.
            document=load_manifest(folder/'result')
            if document.manifest['backend']!='sam3-image':
                raise RuntimeError('Model subprocess returned a non-SAM3 backend')
            input_sha=hashlib.sha256(next(folder.glob('input.*')).read_bytes()).hexdigest()
            if document.manifest['sourceSha256']!=input_sha:
                raise RuntimeError('Model result source SHA256 mismatch with submitted image')
            _done(config,jid,origin='local-model')
        except Exception as exc:
            try:
                if _status(config,jid)['state']=='cancelled':return
                state=_status(config,jid)
                state.update(state='failed',error=str(exc)[:500]);_atomic_json(folder/'status.json',state)
            except (OSError,HTTPException):pass

    @app.post(f'{API}/jobs',status_code=202,dependencies=[Depends(authorized)])
    async def create_job(image: UploadFile=File(...),prompts: str=Form(...)):
        if not _ready(config):
            raise HTTPException(503,{'code':'MODEL_UNAVAILABLE',
               'message':'Install official sam3, mount licensed SAM3 image checkpoint, set environment, and select device. Server demo/import still work.'})
        words=[x.strip() for x in re.split(r'[,，;；\n]+',prompts) if x.strip()]
        if not 1 <= len(words) <= 63 or any(len(x)>128 for x in words):
            raise HTTPException(422,'Please supply 1..63 short nouns in back-to-front order')
        data=await image.read(MAX_SOURCE_BYTES+1)
        ext=_validate_upload(data,image.filename or '')
        # One active local inference per instance. SAM3's vision encoder is
        # expensive: multiplying concurrent processes easily exhausts a GPU.
        with lock:
            for active_status in config.root.glob('*/status.json'):
                try:
                    if json.loads(active_status.read_text(encoding='utf-8')).get('state') in ('queued','running'):
                        raise HTTPException(429,'A SAM3 inference is already active; retry after it finishes')
                except (OSError,ValueError):
                    continue
            jid=str(uuid4());folder=config.root/jid;folder.mkdir()
            input_file=folder/('input'+ext);input_file.write_bytes(data)
            _atomic_json(folder/'status.json',{'id':jid,'state':'queued','backend':'sam3-image',
                            'ai_inference':False,'layers':[], 'message':'Model inference pending'})
        args=[config.python,str(config.script),'--input',str(input_file),'--output',str(folder/'result'),
              '--checkpoint',str(config.checkpoint),'--device',config.device]
        for noun in words:args+=['--prompt',noun]
        Thread(target=_work,args=(jid,args),daemon=True).start()
        return _status(config,jid)

    @app.delete(f'{API}/jobs/{{jid}}',dependencies=[Depends(authorized)])
    def cancel_job(jid: str):
        state=get_job(jid)
        if state['state'] not in ('queued','running'):
            raise HTTPException(409,'Job is not active')
        with lock:
            state['state']='cancelled';_atomic_json(config.root/jid/'status.json',state)
            proc=processes.get(jid)
            if proc and proc.poll() is None:proc.kill()
        return state

    def doc_for(jid: str):
        state=get_job(jid)
        if state['state']!='succeeded':raise HTTPException(409,'Job is not ready')
        try:return load_manifest(config.root/jid/'result')
        except ManifestError as exc:raise HTTPException(409,'Artifact integrity verification failed: '+str(exc)) from exc

    @app.get(f'{API}/jobs/{{jid}}/manifest.json',dependencies=[Depends(authorized)])
    def manifest(jid: str):
        doc=doc_for(jid)
        return doc.manifest

    @app.get(f'{API}/jobs/{{jid}}/preview.png',dependencies=[Depends(authorized)])
    def preview(jid: str):
        doc=doc_for(jid)
        return Response(preview_png(doc),media_type='image/png')

    @app.get(f'{API}/jobs/{{jid}}/layers/{{index}}.png',dependencies=[Depends(authorized)])
    def layer(jid: str,index: int):
        doc=doc_for(jid)
        if index<0 or index>=len(doc.layers):raise HTTPException(404,'Layer not found')
        return Response(png_bytes(rgba_layer(doc,index)),media_type='image/png')

    @app.get(f'{API}/jobs/{{jid}}/layers.ora',dependencies=[Depends(authorized)])
    def ora(jid: str):
        doc=doc_for(jid)
        return Response(ora_bytes(doc),media_type='image/openraster',
                        headers={'Content-Disposition':f'attachment; filename="drawborad-{jid}.ora"'})

    @app.get(f'{API}/jobs/{{jid}}/worker.log',dependencies=[Depends(authorized)])
    def worker_log(jid: str):
        get_job(jid)
        file=config.root/jid/'worker.log'
        if not file.is_file():raise HTTPException(404,'No worker log yet')
        return Response(file.read_bytes()[-12000:],media_type='text/plain; charset=utf-8')

    return app


app=create_app()
