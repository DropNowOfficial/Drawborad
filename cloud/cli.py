"""Portable commands for cloud computers, GitHub runners and coding agents."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from .app import BASE, _settings
from .demo import make_demo
from .exports import ora_bytes, preview_png
from .manifest import load_manifest


def main(argv=None) -> int:
    parser=argparse.ArgumentParser(prog='drawborad-cloud',description='Agent-operable segmentation and object layers')
    sub=parser.add_subparsers(dest='command',required=True)
    server=sub.add_parser('serve',help='Run the cloud Studio and REST API')
    server.add_argument('--host',default='127.0.0.1')
    server.add_argument('--port',type=int,default=8765)
    sample=sub.add_parser('smoke',help='Create an explicitly synthetic sample and OpenRaster artifact')
    sample.add_argument('--output',type=Path,required=True)
    inspect=sub.add_parser('inspect',help='Verify a SAM3 result and report masks and hashes')
    inspect.add_argument('--dir',type=Path,required=True)
    export=sub.add_parser('export',help='Export a SAM3 result to layered OpenRaster')
    export.add_argument('--dir',type=Path,required=True)
    export.add_argument('--ora',type=Path,required=True)
    segment=sub.add_parser('segment',help='Invoke real installed official SAM3 image model')
    segment.add_argument('--image',type=Path,required=True)
    segment.add_argument('--output',type=Path,required=True)
    segment.add_argument('--checkpoint',type=Path,default=None)
    segment.add_argument('--device',choices=['cpu','cuda'],default='cuda')
    segment.add_argument('--prompt',action='append',required=True)
    args=parser.parse_args(argv)
    if args.command=='serve':
        if args.host not in ('127.0.0.1','localhost','::1') and not os.getenv('DRAWBORAD_CLOUD_TOKEN', '').strip():
            print('Refusing public binding without DRAWBORAD_CLOUD_TOKEN; use loopback or set an access TOKEN.',file=sys.stderr)
            return 2
        import uvicorn
        uvicorn.run('cloud.app:app',host=args.host,port=args.port,log_level='info')
        return 0
    if args.command=='smoke':
        make_demo(args.output)
        doc=load_manifest(args.output)
        ora=args.output/'Drawborad_sample.ora'
        ora.write_bytes(ora_bytes(doc))
        (args.output/'preview.png').write_bytes(preview_png(doc))
        print(json.dumps({'backend':doc.manifest['backend'],'ai_inference':False,
                          'layers':[x.name for x in doc.layers],
                          'ora':str(ora.resolve()), 'manifest':str(args.output/'manifest.json')}))
        return 0
    if args.command=='inspect':
        doc=load_manifest(args.dir)
        print(json.dumps({'backend':doc.manifest['backend'],'ai_inference':doc.is_ai,
                          'width':doc.width,'height':doc.height,
                          'layers':[{'name':x.name,'score':x.score,'pixels':x.count} for x in doc.layers]},ensure_ascii=False))
        return 0
    if args.command=='export':
        doc=load_manifest(args.dir)
        args.ora.parent.mkdir(parents=True,exist_ok=True)
        args.ora.write_bytes(ora_bytes(doc))
        print(args.ora.resolve())
        return 0
    if args.command=='segment':
        config=_settings()
        checkpoint=args.checkpoint or config.checkpoint
        if checkpoint is None or not checkpoint.is_file():
            print('Missing licensed SAM3 image checkpoint: --checkpoint or DRAWBORAD_SAM3_CHECKPOINT.',file=sys.stderr)
            return 2
        if not args.image.is_file() or not config.script.is_file():
            print('Missing source image or official SAM3 adapter script.',file=sys.stderr)
            return 2
        command=[config.python,str(config.script),'--input',str(args.image),'--output',str(args.output),
                 '--checkpoint',str(checkpoint),'--device',args.device]
        for phrase in args.prompt:
            command.extend(['--prompt',phrase])
        run=subprocess.run(command,check=False)
        if run.returncode:return run.returncode
        doc=load_manifest(args.output)
        print(json.dumps({'backend':doc.manifest['backend'],'ai_inference':True,
                          'manifest':str(args.output/'manifest.json'),
                          'objects':[x.name for x in doc.layers]},ensure_ascii=False))
        return 0
    return 2


if __name__=='__main__':
    try:
        raise SystemExit(main())
    except (ValueError,OSError) as exc:
        print('Drawborad cloud error:',str(exc),file=sys.stderr)
        raise SystemExit(2) from exc
