"""One finite worker invocation. No worker waits for human review."""
import argparse,os,time
from .runtime import make_jobs

def execute(jid,phase):
    jobs=make_jobs()
    if phase=='segment':
        from .sam3_worker import segment
        return jobs.segment(jid,segment)
    if phase=='build':return jobs.build(jid)
    raise ValueError('Invalid phase')

def handler(event):
    data=event['input']
    try:
        j=execute(data['job_id'],data['phase'])
        return {'job_id':j['id'],'state':j['state'],'revision':j['revision']}
    except Exception as exc:
        # RunPod recognizes the error key; never report an ordinary success on failure.
        return {'error':type(exc).__name__,'job_id':data.get('job_id')}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--job-id');parser.add_argument('--phase',choices=['segment','build']);parser.add_argument('--runpod',action='store_true');args=parser.parse_args()
    os.environ['DRAWBORAD_WORKER_STARTED_AT']=str(time.time())
    if args.runpod:
        import runpod
        runpod.serverless.start({'handler':handler})
    elif args.job_id and args.phase:execute(args.job_id,args.phase)
    else:parser.error('Provide job-id and phase or --runpod')
