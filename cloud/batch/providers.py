"""Private S3/R2 storage and bounded, non-retrying RunPod submission.

These adapters require real-account qualification; unit tests are not GPU evidence.
"""
import json,re,time
from decimal import Decimal
from .jobs import Conflict,encoded,safe_key

class S3Store:
    def __init__(self,client,bucket,prefix='drawborad'):
        self.client=client;self.bucket=bucket;self.prefix=safe_key(prefix)
    def read(self,key):
        try:
            r=self.client.get_object(Bucket=self.bucket,Key=self.prefix+'/'+safe_key(key))
            return r['Body'].read(),r['ETag']
        except self.client.exceptions.NoSuchKey:return None,None
    def put(self,key,body,expected='*'):
        kwargs=dict(Bucket=self.bucket,Key=self.prefix+'/'+safe_key(key),Body=body,ContentType='application/octet-stream')
        if expected is None:kwargs['IfNoneMatch']='*'
        elif expected!='*':kwargs['IfMatch']=expected
        try:return self.client.put_object(**kwargs)['ETag']
        except Exception as exc:
            if getattr(exc,'response',{}).get('ResponseMetadata',{}).get('HTTPStatusCode') in (409,412):raise Conflict('Conditional object write conflict') from exc
            raise

class RunPodDispatcher:
    def __init__(self,store,endpoint,api_key,client=None,approved_usd='0',cpu_dispatch=None):
        import httpx
        self.store=store;self.endpoint=endpoint;self.key=api_key;self.client=client or httpx.Client(timeout=30)
        self.approved=Decimal(approved_usd);self.cpu_dispatch=cpu_dispatch
    def __call__(self,jid,phase):
        if phase=='build':
            if self.cpu_dispatch is None:raise ValueError('No on-demand CPU provider configured; GPU fallback disabled')
            return self.cpu_dispatch(jid,phase)
        if phase!='segment' or not re.fullmatch('[0-9a-f]{32}',jid):raise ValueError('Invalid dispatch')
        if not self.approved.is_finite() or not 0<self.approved<=10:raise ValueError('Explicit first-trial budget (up to $10) required')
        if not re.fullmatch('[A-Za-z0-9_-]+',self.endpoint) or not self.key:raise ValueError('Provider credentials/configuration missing')
        # One paid trial is reserved durably. An operator must reconcile provider usage
        # before opening another budget; lost HTTP replies must never trigger /run again.
        self.store.put('budget/first-trial.json',encoded(dict(job_id=jid,reserved_usd=str(self.approved),state='reserved',at=time.time())),None)
        key=f'jobs/{jid}/dispatch.json'
        tag=self.store.put(key,encoded(dict(state='submitting',endpoint=self.endpoint,at=time.time())),None)
        try:
            r=self.client.post(f'https://api.runpod.ai/v2/{self.endpoint}/run',headers={'Authorization':'Bearer '+self.key},json={'input':{'job_id':jid,'phase':'segment'},'policy':{'executionTimeout':180000,'ttl':600000}})
            r.raise_for_status();provider_id=r.json()['id']
            self.store.put(key,encoded(dict(state='submitted',provider_job_id=provider_id,endpoint=self.endpoint,at=time.time())),tag)
            return provider_id
        except Exception:
            self.store.put(key,encoded(dict(state='submission_unknown',endpoint=self.endpoint,at=time.time(),message='Reconcile provider job before any retry; budget remains reserved')),tag)
            raise
    def cancel(self,jid):
        b,_=self.store.read(f'jobs/{jid}/dispatch.json')
        if not b:return
        record=json.loads(b)
        if record['state']!='submitted':raise Conflict('Submission uncertain; cancel/reconcile in provider console')
        pid=record['provider_job_id']
        if not re.fullmatch('[A-Za-z0-9_-]+',pid):raise ValueError('Invalid provider ID')
        self.client.post(f'https://api.runpod.ai/v2/{self.endpoint}/cancel/{pid}',headers={'Authorization':'Bearer '+self.key}).raise_for_status()
