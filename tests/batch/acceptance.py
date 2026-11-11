"""Independent HTTP + optional Chromium acceptance. Fixture is explicitly NOT SAM3."""
import argparse,hashlib,json,os,secrets,socket,subprocess,sys,time
from pathlib import Path
import httpx
ROOT=Path(__file__).resolve().parents[2]

def run(engine,image,out,browser=False):
    out=out.resolve();out.mkdir(parents=True,exist_ok=True)
    token=secrets.token_urlsafe(32)
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    url=f'http://127.0.0.1:{port}';prefix='/api/v1/generation/jobs'
    env=dict(os.environ,DRAWBORAD_CLOUD_TOKEN=token,DRAWBORAD_BATCH_FIXTURE='1',DRAWBORAD_BATCH_DATA=str(out/'store'),DRAWBORAD_NATIVE_ENGINE=str(engine.resolve()))
    log=open(out/'gateway.log','w');proc=None
    client=httpx.Client(base_url=url,headers={'Authorization':'Bearer '+token},timeout=20,trust_env=False)
    def launch():
        p=subprocess.Popen([sys.executable,'-m','uvicorn','cloud.batch.runtime:make_app','--factory','--host','127.0.0.1','--port',str(port)],cwd=ROOT,env=env,stdout=log,stderr=log)
        for _ in range(100):
            try:
                if client.get('/').status_code==200:return p
            except httpx.TransportError:pass
            if p.poll() is not None:raise AssertionError('Gateway failed; see gateway.log')
            time.sleep(.1)
        p.terminate();p.wait();raise AssertionError('Gateway startup timed out')
    def req(method,path,**kwargs):
        r=client.request(method,path,**kwargs);r.raise_for_status();return r
    def await_state(jid,state):
        for _ in range(600):
            j=req('GET',prefix+'/'+jid).json()
            if j['state']==state:return j
            assert j['state'] not in ('failed','timed_out','cancelled'),j
            time.sleep(.2)
        raise AssertionError('Job did not complete')
    try:
        proc=launch()
        assert httpx.get(url+'/api/v1/generation/capabilities',trust_env=False).status_code==401
        body=image.read_bytes();kwargs=dict(headers={'Idempotency-Key':'http-acceptance'},files={'image':(image.name,body)},data={'backend':'fixture-no-ai'})
        j=req('POST',prefix,**kwargs).json();jid=j['id'];p=prefix+'/'+jid
        assert req('POST',prefix,**kwargs).json()['id']==jid
        req('POST',p+'/start');j=await_state(jid,'awaiting_review')
        assert not j['accepted'] and not j['inference_ran']
        # Worker has returned; destroy gateway too, then restore only from storage.
        proc.terminate();proc.wait(timeout=10);proc=launch()
        assert req('GET',p).json()['candidates']==j['candidates']
        assert client.post(p+'/accept',json={'ids':[]}).status_code==422
        selected=j['candidates'][0]['id']
        (out/'candidate.png').write_bytes(req('GET',p+'/candidates/'+selected+'.png').content)
        req('POST',p+'/accept',json={'ids':[selected]});req('POST',p+'/build')
        j=await_state(jid,'completed')
        exports=out/'artifacts';exports.mkdir(exist_ok=True)
        for name,asset in j['outputs'].items():
            data=req('GET',p+'/files/'+name).content
            assert hashlib.sha256(data).hexdigest()==asset['sha256']
            (exports/name).write_bytes(data)
        proc.terminate();proc.wait(timeout=10);proc=launch()
        assert req('GET',p+'/files/project.draw').content==(exports/'project.draw').read_bytes()
        sys.path.insert(0,str(ROOT/'tests/headless'));from verify_artifacts import verify
        verify(exports)
        report=json.loads((exports/'report.json').read_text())
        assert report['objectCount']==1 and report['strokeEvents']>0
        assert report['replaySourceAssetReads']==0 and j['cost']['source_removed_replay']
        cancelled=req('POST',prefix,headers={'Idempotency-Key':'cancel'},files={'image':(image.name,body)},data={'backend':'fixture-no-ai'}).json()
        cp=prefix+'/'+cancelled['id'];req('POST',cp+'/cancel');assert client.post(cp+'/start').status_code==409
        receipt=dict(http='passed',backend='fixture-no-ai',sam3_ran=False,gpu_billing_verified=False,actual_gpu_cost_usd=None,input_sha256=hashlib.sha256(body).hexdigest(),native_report=report,source_free_replay=True,review_and_download_after_gateway_restart=True,browser='not_run')
        if browser:
            from playwright.sync_api import sync_playwright
            with sync_playwright() as pw:
                b=pw.chromium.launch(headless=True);page=b.new_page(viewport={'width':1280,'height':1000})
                page.goto(url);page.locator('#token').fill(token);page.locator('#connect').click()
                page.locator('#fixtureOption').wait_for(state='visible');page.locator('#fixture').check();page.locator('#image').set_input_files(str(image.resolve()))
                page.locator('#upload').click();page.wait_for_function("document.querySelector('#start').disabled===false")
                page.locator('#start').click();page.wait_for_function("document.querySelectorAll('#candidates input').length>0")
                page.screenshot(path=str(out/'browser-review.png'),full_page=True)
                page.locator('#candidates input').first.check();page.locator('#build').click()
                page.locator('#downloads a').first.wait_for(state='visible',timeout=180000)
                page.screenshot(path=str(out/'browser-completed.png'),full_page=True)
                with page.expect_download() as dl:page.locator('#downloads a').first.click()
                dl.value.save_as(str(out/'browser-project.draw'))
                before=(out/'browser-project.draw').read_bytes()
                page.reload();page.locator('#token').fill(token);page.locator('#connect').click()
                page.locator('#downloads a').first.wait_for(state='visible')
                with page.expect_download() as dl:page.locator('#downloads a').first.click()
                dl.value.save_as(str(out/'browser-reopened.draw'))
                assert before==(out/'browser-reopened.draw').read_bytes();b.close()
                receipt['browser']='passed'
        (out/'acceptance.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
    finally:
        client.close()
        if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=10)
        log.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('engine',type=Path);p.add_argument('image',type=Path);p.add_argument('out',type=Path);p.add_argument('--browser',action='store_true');a=p.parse_args();run(a.engine,a.image,a.out,a.browser)
