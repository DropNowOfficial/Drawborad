#!/usr/bin/env python3
"""Run explicit, bounded research fixtures twice. Execution is not art approval."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def read_manifest(path):
    if not path.is_file() or path.stat().st_size>1024*1024:raise ValueError('manifest file budget')
    with path.open('r',encoding='utf-8') as handle:text=handle.read(1024*1024+1)
    if len(text)>1024*1024:raise ValueError('manifest file budget')
    data=json.loads(text)
    if not isinstance(data,dict) or not isinstance(data.get('fixtures'),list) or not 1<=len(data['fixtures'])<=256:
        raise ValueError('fixtures must contain 1..256 entries')
    ids=set()
    for f in data['fixtures']:
        if not isinstance(f,dict):raise ValueError('fixture must be an object')
        name=f.get('id')
        if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*',name) or name in ids:raise ValueError('unique safe fixture id required')
        ids.add(name)
        if any(type(f.get(k)) is not int or not 1<=f[k]<=512 for k in ('width','height')):raise ValueError('dimension budget')
        masks=f.get('masks')
        if not isinstance(masks,list) or not 1<=len(masks)<=16:raise ValueError('object budget')
        if any(not isinstance(v,str) or not v.strip() or '\0' in v for v in [f.get('rgba'),*masks]):raise ValueError('input paths required')
    return data


def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--id',action='append',dest='ids');p.add_argument('--native-runner',type=Path,default=Path(__file__).parent/'build/native-hybrid')
    args=p.parse_args()
    try:
        data=read_manifest(args.manifest)
        if args.ids and not set(args.ids)<={f['id'] for f in data['fixtures']}:raise ValueError('requested fixture id is missing')
        fixtures=[f for f in data['fixtures'] if not args.ids or f['id'] in args.ids]
        if not fixtures:raise ValueError('no selected fixtures')
    except (OSError,ValueError) as error:p.error(f'invalid manifest: {error}')
    if not args.native_runner.is_file():p.error('native runner is missing; build it first')
    if args.output.exists():p.error('output exists; no clobber')
    args.output.mkdir(parents=True);root=Path(__file__).resolve().parent;runs=[];failures=[]
    for f in fixtures:
        name=f['id'];source=args.manifest.resolve().parent/f['rgba'];masks=[args.manifest.resolve().parent/m for m in f['masks']];paths=[]
        try:
            for repeat in (1,2):
                pd=args.output/f'{name}-plan-{repeat}';nd=args.output/f'{name}-native-{repeat}'
                cmd=[sys.executable,str(root/'hybrid.py'),'--research','--rgba',str(source),'--width',str(f['width']),'--height',str(f['height']),'--output',str(pd)]
                for mask in masks:cmd+=['--mask',str(mask)]
                planned=subprocess.run(cmd,capture_output=True,text=True,timeout=180)
                (args.output/f'{name}-planner-{repeat}.log').write_text(planned.stdout+planned.stderr)
                if planned.returncode:raise RuntimeError(f'planner exit {planned.returncode}: '+(planned.stdout+planned.stderr).strip()[-1200:])
                native=subprocess.run([str(args.native_runner.resolve()),str(pd/'plan.txt'),str(source),str(nd),*[str(m) for m in masks]],capture_output=True,text=True,timeout=180)
                (args.output/f'{name}-native-{repeat}.log').write_text(native.stdout+native.stderr)
                if native.returncode:raise RuntimeError('native rejected: '+native.stderr.strip())
                if (pd/'feedback.rgba').read_bytes()!=(nd/'final.rgba').read_bytes():raise RuntimeError('native feedback/final mismatch')
                paths.append(nd)
            a,b=[json.loads((nd/'audit.json').read_text()) for nd in paths]
            frames={stage:(paths[0]/f'{stage}.rgba').read_bytes()==(paths[1]/f'{stage}.rgba').read_bytes() for stage in ['zero','base','form','detail','final','replay-25','replay-50','replay-75']}
            if a['events']!=b['events'] or a['layers']!=b['layers'] or not all(frames.values()):raise RuntimeError('native repeat differs')
            runs.append(dict(fixture=name,variant='native-hybrid-v3-flat-research',nativeOutput=str((paths[0]/'final.rgba').resolve()),metadata=str((paths[0]/'audit.json').resolve()),repeatOutput=str((paths[1]/'final.rgba').resolve()),repeatMetadata=str((paths[1]/'audit.json').resolve()),repeatEventsAndLayersEqual=True,repeatFrameByteIdentity=frames))
            print(f'EXECUTED {name}: {len(a["events"])} native events; quality not evaluated; two runs identical',flush=True)
        except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
            failures.append(dict(fixture=name,error=str(error)));print(f'BLOCKED {name}: {error}',flush=True)
        summary=dict(scope='execution-only; not art-quality acceptance',plannerSha256=hashlib.sha256((root/'hybrid.py').read_bytes()).hexdigest(),runs=runs,failures=failures)
        (args.output/'runs.json').write_text(json.dumps(summary,indent=2))
    return 1 if failures else 0

if __name__=='__main__':raise SystemExit(main())
