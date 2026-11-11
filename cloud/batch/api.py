"""Stateless single-owner gateway; deploy on a scale-to-zero HTTP container."""
import json,secrets
from pathlib import Path
from fastapi import FastAPI,Depends,Header,HTTPException,UploadFile,File,Form,Request
from fastapi.responses import HTMLResponse,Response,JSONResponse
from .jobs import Conflict,MAX_SOURCE_BYTES
from PIL import UnidentifiedImageError

def create_app(jobs,token,dispatch,allow_fixture=False,cancel_remote=None):
    if not token:raise ValueError('Batch gateway requires owner authentication')
    app=FastAPI(title='Drawborad finite generation')
    def auth(authorization:str=Header(default='')):
        if not secrets.compare_digest(authorization,'Bearer '+token):raise HTTPException(401,'Owner token required')
    @app.exception_handler(Conflict)
    async def conflict(_,exc):return JSONResponse({'detail':str(exc)},status_code=409)
    @app.exception_handler(UnidentifiedImageError)
    async def invalid_image(_,exc):return JSONResponse({"detail":"Invalid PNG/JPEG image"},status_code=422)
    @app.exception_handler(ValueError)
    async def invalid(_,exc):return JSONResponse({'detail':str(exc)},status_code=422)
    @app.exception_handler(KeyError)
    async def missing(_,exc):return JSONResponse({'detail':'Not found'},status_code=404)
    @app.get('/',response_class=HTMLResponse)
    def page():return Path(__file__).with_name('index.html').read_text()
    prefix='/api/v1/generation'
    @app.get(prefix+'/capabilities',dependencies=[Depends(auth)])
    def capabilities():return dict(fixture_enabled=allow_fixture,editing_session=False,automatic_layout=False)
    @app.post(prefix+'/jobs',status_code=201,dependencies=[Depends(auth)])
    async def upload(image:UploadFile=File(...),backend:str=Form('sam3-image'),prompts:str=Form('[]'),views:str=Form('[]'),idempotency_key:str=Header(...)):
        if backend=='fixture-no-ai' and not allow_fixture:raise HTTPException(422,'Fixture mode disabled')
        content=await image.read(MAX_SOURCE_BYTES+1)
        return jobs.create(content,idempotency_key,backend,json.loads(prompts),json.loads(views))
    @app.get(prefix+'/jobs/{jid}',dependencies=[Depends(auth)])
    def status(jid:str):return jobs.get(jid)
    def start(jid,phase):
        if jobs.get(jid)['state']!=phase+'_queued':raise Conflict('Task is not queued')
        dispatch(jid,phase);return jobs.get(jid)
    @app.post(prefix+'/jobs/{jid}/start',status_code=202,dependencies=[Depends(auth)])
    def segment(jid:str):return start(jid,'segment')
    @app.post(prefix+'/jobs/{jid}/accept',dependencies=[Depends(auth)])
    async def accept(jid:str,request:Request):
        body=await request.json()
        if not isinstance(body,dict):raise ValueError('Object selection must be a JSON object')
        return jobs.accept(jid,body.get('ids'))
    @app.post(prefix+'/jobs/{jid}/build',status_code=202,dependencies=[Depends(auth)])
    def build(jid:str):return start(jid,'build')
    @app.post(prefix+'/jobs/{jid}/cancel',dependencies=[Depends(auth)])
    def cancel(jid:str):
        j=jobs.cancel(jid)
        if cancel_remote:
            try:cancel_remote(jid)
            except Exception:
                j=jobs.record_remote_cancel(jid,'pending')
                return JSONResponse(j,status_code=202)
            return jobs.record_remote_cancel(jid,'requested')
        return j
    @app.get(prefix+'/jobs/{jid}/candidates/{cid}.png',dependencies=[Depends(auth)])
    def candidate(jid:str,cid:str):
        row=next((x for x in jobs.get(jid)['candidates'] if x['id']==cid),None)
        if not row:raise KeyError(cid)
        return Response(jobs.read_asset(row['preview']),media_type='image/png',headers={'Cache-Control':'no-store'})
    @app.get(prefix+'/jobs/{jid}/files/{name}',dependencies=[Depends(auth)])
    def download(jid:str,name:str):
        j=jobs.get(jid)
        if j['state']!='completed':raise Conflict('Artifacts are not ready')
        if name not in j['outputs']:raise KeyError(name)
        return Response(jobs.read_asset(j['outputs'][name]),media_type='application/octet-stream',headers={'Content-Disposition':f'attachment; filename="{name}"','Cache-Control':'no-store'})
    return app
