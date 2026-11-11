"""Durable job orchestration only. Painting remains in the native .draw file."""
from __future__ import annotations
import copy,fcntl,hashlib,io,json,os,re,shutil,subprocess,tempfile,time,uuid
from pathlib import Path
import numpy as np
from PIL import Image,ImageOps
from cloud.manifest import load_manifest,MAX_PIXELS,MAX_SOURCE_BYTES
from cloud.exports import rgba_layer,png_bytes

class Conflict(RuntimeError):pass

def digest(b):return hashlib.sha256(b).hexdigest()
def encoded(value):return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def safe_key(key):
    if not re.fullmatch(r'[a-zA-Z0-9/_\-.]+',key) or any(x in ('','..','.') for x in key.split('/')):raise ValueError('Invalid storage key')
    return key

class LocalStore:
    """Persistent development store with cross-process compare-and-swap."""
    def __init__(self,root):self.root=Path(root).resolve();self.root.mkdir(parents=True,exist_ok=True)
    def read(self,key):
        p=self.root/safe_key(key)
        if not p.exists():return None,None
        b=p.read_bytes();return b,digest(b)
    def put(self,key,body,expected='*'):
        p=self.root/safe_key(key);p.parent.mkdir(parents=True,exist_ok=True)
        with open(str(p)+'.lock','a+b') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            _,current=self.read(key)
            if expected!='*' and expected!=current:raise Conflict('State changed; reload')
            fd,tmp=tempfile.mkstemp(dir=p.parent)
            try:
                with os.fdopen(fd,'wb') as f:f.write(body);f.flush();os.fsync(f.fileno())
                os.replace(tmp,p)
            finally:
                if os.path.exists(tmp):os.unlink(tmp)
        return digest(body)

class Jobs:
    def __init__(self,store,engine=None,timeout=180):
        self.store=store;self.engine=str(Path(engine).resolve()) if engine else None;self.timeout=timeout
    def _key(self,jid):
        if not re.fullmatch('[0-9a-f]{32}',jid):raise ValueError('Invalid job ID')
        return f'jobs/{jid}/state.json'
    def _read(self,jid):
        b,tag=self.store.read(self._key(jid))
        if b is None:raise KeyError('Job not found')
        return json.loads(b),tag
    def get(self,jid):
        j=self._read(jid)[0]
        if j['state'] in ('segment_running','build_running') and time.time()>j['deadline']:
            try:return self.expire(jid)
            except Conflict:return self._read(jid)[0]
        return j
    def _change(self,jid,states,fn):
        j,tag=self._read(jid)
        if j['state'] not in states:raise Conflict('Operation not available in '+j['state'])
        fn(j);j['revision']=str(int(j['revision'])+1);j['updated_at']=time.time()
        self.store.put(self._key(jid),encoded(j),tag);return j
    def asset(self,jid,name,body):
        sha=digest(body);key=f'jobs/{jid}/blobs/{sha}/{safe_key(name)}';self.store.put(key,body)
        return dict(key=key,sha256=sha,bytes=len(body))
    def read_asset(self,record):
        b,_=self.store.read(record['key'])
        if b is None or len(b)!=record['bytes'] or digest(b)!=record['sha256']:raise ValueError('Artifact integrity check failed')
        return b
    def create(self,image,key,backend,prompts=None,views=None):
        if not isinstance(key,str) or not 1<=len(key)<=128:raise ValueError('Idempotency key required')
        if backend not in ('fixture-no-ai','sam3-image'):raise ValueError('Unsupported backend')
        if len(image)>MAX_SOURCE_BYTES:raise ValueError('Image exceeds 32MiB')
        with Image.open(io.BytesIO(image)) as raw:
            if raw.format not in ('PNG','JPEG'):raise ValueError('PNG/JPEG required')
            im=ImageOps.exif_transpose(raw);w,h=im.size
            if min(w,h)<2 or max(w,h)>4096 or w*h>MAX_PIXELS:raise ValueError('Unsupported original dimensions; no silent resize')
            alpha=np.asarray(im.convert('RGBA').getchannel('A'))
            if np.any((alpha!=0)&(alpha!=255)):raise ValueError('Partial alpha unsupported')
            name='source.png' if raw.format=='PNG' else 'source.jpg'
        prompts=prompts or [];views=views or []
        if not isinstance(prompts,list) or len(prompts)>16 or any(not isinstance(p,str) or not p.strip() or len(p)>64 for p in prompts):raise ValueError('Invalid prompts')
        if backend=='sam3-image' and not prompts:raise ValueError('Object prompts required')
        # Confirmed manual layout regions retain distinct identities; no automatic layout claim.
        if not isinstance(views,list) or len(views)>8:raise ValueError('Invalid views')
        used=[];names=set()
        for v in views:
            if not isinstance(v,dict) or v.get('confirmed') is not True or not re.fullmatch('[a-zA-Z0-9_-]{1,24}',str(v.get('id',''))) or v['id'] in names:raise ValueError('Views must have distinct confirmed IDs')
            box=v.get('box')
            if not isinstance(box,list) or len(box)!=4 or any(type(n)!=int for n in box):raise ValueError('Invalid view box')
            x,y,r,b=box
            if not 0<=x<r<=w or not 0<=y<b<=h:raise ValueError('View outside image')
            if any(max(x,a)<min(r,c) and max(y,d)<min(b,e) for a,d,c,e in used):raise ValueError('Views overlap')
            names.add(v['id']);used.append(box)
        spec=dict(sourceSha256=digest(image),backend=backend,prompts=prompts,views=views)
        jid=digest(key.encode())[:32];fingerprint=digest(encoded(spec));old,_=self.store.read(self._key(jid))
        if old:
            j=json.loads(old)
            if j['fingerprint']!=fingerprint:raise Conflict('Idempotency key reused for different input')
            return j
        j=dict(id=jid,revision='0',state='segment_queued',backend=backend,fingerprint=fingerprint,input=self.asset(jid,name,image),image_name=name,width=w,height=h,prompts=prompts,views=views,candidates=[],accepted=[],outputs={},inference_ran=False,created_at=time.time(),cost=dict(actual_usd=None,gpu_inference_ran=False,billing_status='not_run',gpu_billed_seconds=None,storage_usd=None,network_usd=None))
        try:self.store.put(self._key(jid),encoded(j),None)
        except Conflict:return self.create(image,key,backend,prompts,views)
        return j
    def claim(self,jid,phase):
        if phase not in ('segment','build'):raise ValueError('Invalid phase')
        def change(j):
            j.update(state=phase+'_running',attempt=uuid.uuid4().hex,deadline=time.time()+self.timeout,started_at=time.time())
            if phase=='segment' and j['backend']=='sam3-image':j['cost']['billing_status']='unreconciled'
        return self._change(jid,[phase+'_queued'],change)
    def cancel(self,jid):
        return self._change(jid,['segment_queued','segment_running','awaiting_review','build_queued','build_running'],lambda j:j.update(state='cancelled',cancelled_at=time.time()))
    def expire(self,jid):
        def change(j):
            if time.time()<j['deadline']:raise Conflict('Task has not timed out')
            j.update(state='timed_out',error='Execution deadline exceeded; provider billing must be reconciled')
        return self._change(jid,['segment_running','build_running'],change)
    def accept(self,jid,ids):
        def change(j):
            allowed={c['id'] for c in j['candidates']}
            if not isinstance(ids,list) or not ids or any(not isinstance(x,str) for x in ids) or len(set(ids))!=len(ids) or not set(ids)<=allowed:raise ValueError('Select existing objects explicitly')
            j.update(accepted=ids,state='build_queued',reviewed_at=time.time())
        return self._change(jid,['awaiting_review'],change)
    def _finish(self,jid,claim,updates):
        def change(j):
            if j['attempt']!=claim['attempt']:raise Conflict('Stale worker result')
            if time.time()>j['deadline']:raise Conflict('Execution deadline exceeded')
            j.update(updates)
        return self._change(jid,[claim['state']],change)
    def _fail(self,jid,claim,exc):
        try:
            def fail(j):
                if j['attempt']!=claim['attempt']:raise Conflict('Stale worker')
                j.update(state='timed_out' if time.time()>j['deadline'] or isinstance(exc,subprocess.TimeoutExpired) else 'failed',error=type(exc).__name__+': '+str(exc)[:300])
            self._change(jid,[claim['state']],fail)
        except Conflict:pass
    def segment(self,jid,segmenter=None):
        j=self.claim(jid,'segment')
        try:
            with tempfile.TemporaryDirectory(prefix='drawborad-segment-') as temp:
                root=Path(temp);source=root/j['image_name'];source.write_bytes(self.read_asset(j['input']));out=root/'result'
                metrics={}
                if j['backend']=='fixture-no-ai':self._fixture(source,out)
                else:
                    if segmenter is None:raise RuntimeError('SAM3 GPU worker unavailable; no fixture substitution')
                    metrics=segmenter(source,out,j)
                doc=load_manifest(out)
                if doc.manifest['backend']!=j['backend']:raise ValueError('Backend provenance mismatch')
                files={p:self.asset(jid,p,(out/p).read_bytes()) for p in ['manifest.json',doc.manifest['image']]+[r['mask'] for r in doc.manifest['objects']]}
                candidates=[]
                for i,row in enumerate(doc.manifest['objects']):
                    cid=digest(encoded([j['input']['sha256'],i,row['name'],row['sha256']]))[:32]
                    candidates.append(dict(id=cid,index=i,name=row['name'],score=row['score'],pixels=row['pixels'],is_background=row['isBackground'],preview=self.asset(jid,f'preview-{i}.png',png_bytes(rgba_layer(doc,i)))))
                return self._finish(jid,j,dict(state='awaiting_review',manifest_files=files,candidates=candidates,inference_ran=j['backend']=='sam3-image',metrics=metrics,segmentation_finished_at=time.time(),cost={**j['cost'],'gpu_inference_ran':j['backend']=='sam3-image','billing_status':'unreconciled' if j['backend']=='sam3-image' else 'local_fixture_no_gpu'}))
        except Exception as exc:self._fail(jid,j,exc);raise
    def _fixture(self,source,out):
        out.mkdir();(out/'masks').mkdir();shutil.copyfile(source,out/source.name)
        with Image.open(source) as raw:
            im=ImageOps.exif_transpose(raw).convert('RGBA');w,h=im.size;visible=np.asarray(im.getchannel('A'))>0
        objects=[]
        for i,part in enumerate((np.indices((h,w))[1]<w//2,np.indices((h,w))[1]>=w//2)):
            mask=part&visible
            if not mask.any():continue
            b=(mask.astype(np.uint8)*255).tobytes();path=f'masks/layer-{len(objects):02d}.u8';(out/path).write_bytes(b)
            objects.append(dict(name=f'Fixture region {i} (not AI)',score=1,isBackground=False,pixels=int(mask.sum()),mask=path,sha256=digest(b)))
        (out/'manifest.json').write_bytes(encoded(dict(schema=1,backend='fixture-no-ai',promptOrder='back-to-front',image=source.name,width=w,height=h,sourceSha256=digest(source.read_bytes()),objects=objects)))
    def build(self,jid):
        j=self.claim(jid,'build');started=time.monotonic()
        try:
            if not self.engine:raise RuntimeError('Native engine unavailable')
            with tempfile.TemporaryDirectory(prefix='drawborad-build-') as temp:
                root=Path(temp);inp=root/'input';inp.mkdir();out=root/'output'
                for name,record in j['manifest_files'].items():
                    p=inp/safe_key(name);p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(self.read_asset(record))
                load_manifest(inp)
                args=[self.engine,'run','--manifest',str(inp/'manifest.json'),'--output',str(out)]
                for c in j['candidates']:
                    if c['id'] in j['accepted']:args+=['--accept-index',str(c['index'])]
                if j['backend']=='fixture-no-ai':args+=['--fixture']
                result=subprocess.run(args,capture_output=True,text=True,timeout=self.timeout)
                if result.returncode:raise RuntimeError('Native reconstruction failed: '+result.stderr[-200:])
                report=json.loads((out/'report.json').read_text())
                if report['strokeEvents']<=0 or report['objectCount']!=len(j['accepted']):raise ValueError('Native object/stroke receipt mismatch')
                shutil.rmtree(inp)
                requests=[dict(request_id='open',method='open',params=dict(path=str(out/'project.draw'))),dict(request_id='replay',method='export',params=dict(path=str(root/'replay.png'),format='png'))]
                replay=subprocess.run([self.engine,'session'],input=''.join(json.dumps(x)+'\n' for x in requests),text=True,capture_output=True,timeout=self.timeout)
                responses=[json.loads(s) for s in replay.stdout.splitlines()]
                if replay.returncode or len(responses)!=2 or any('error' in r for r in responses):raise ValueError('Source-free reopen/replay failed')
                with Image.open(root/'replay.png') as a,Image.open(out/'final.png') as b:
                    if a.convert('RGBA').tobytes()!=b.convert('RGBA').tobytes():raise ValueError('Reopened rendering differs')
                cost={**j['cost'],'cpu_reconstruction_seconds':time.monotonic()-started,'source_removed_replay':True}
                (out/'cost.json').write_bytes(encoded(cost))
                outputs={name:self.asset(jid,name,(out/name).read_bytes()) for name in ['project.draw','final.png','painted.ora','zero.png','middle.png','report.json','cost.json']}
                return self._finish(jid,j,dict(state='completed',outputs=outputs,cost=cost,completed_at=time.time()))
        except Exception as exc:self._fail(jid,j,exc);raise
