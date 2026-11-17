"""Environment wiring. Local fixture mode is explicit and never enables SAM3."""
import os,subprocess,sys,threading
from .jobs import Jobs,LocalStore,encoded
from .providers import S3Store,RunPodDispatcher

def make_jobs():
    if os.environ.get('DRAWBORAD_BATCH_STORE')=='s3':
        import boto3
        from botocore.config import Config
        client=boto3.client('s3',endpoint_url=os.environ['DRAWBORAD_S3_ENDPOINT'],region_name=os.environ.get('AWS_DEFAULT_REGION','auto'),config=Config(retries={'max_attempts':0}))
        store=S3Store(client,os.environ['DRAWBORAD_S3_BUCKET'],os.environ.get('DRAWBORAD_S3_PREFIX','drawborad'))
    else:store=LocalStore(os.environ.get('DRAWBORAD_BATCH_DATA','.batch-data'))
    return Jobs(store,os.environ.get('DRAWBORAD_NATIVE_ENGINE'),planner=os.environ.get('DRAWBORAD_BATCH_PLANNER','sweep'))

def make_app():
    from .api import create_app
    jobs=make_jobs();fixture=os.environ.get('DRAWBORAD_BATCH_FIXTURE')=='1'
    if fixture:
        if os.environ.get('DRAWBORAD_BATCH_STORE')=='s3':raise ValueError('Local fixture launcher requires local development store')
        def dispatch(jid,phase):
            if jobs.get(jid)['backend']!='fixture-no-ai':raise ValueError('Local fixture launcher cannot run paid inference')
            jobs.store.put(f'jobs/{jid}/{phase}-dispatch.json',encoded({'state':'launched'}),None)
            p=subprocess.Popen([sys.executable,'-m','cloud.batch.worker','--job-id',jid,'--phase',phase],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            threading.Thread(target=p.wait,daemon=True).start()
        cancel=None
    else:
        dispatch=RunPodDispatcher(jobs.store,os.environ.get('RUNPOD_ENDPOINT_ID',''),os.environ.get('RUNPOD_API_KEY',''),approved_usd=os.environ.get('DRAWBORAD_TRIAL_APPROVED_USD','0'))
        cancel=dispatch.cancel
    return create_app(jobs,os.environ.get('DRAWBORAD_CLOUD_TOKEN',''),dispatch,fixture,cancel)
