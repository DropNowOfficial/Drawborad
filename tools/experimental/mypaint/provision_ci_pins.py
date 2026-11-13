#!/usr/bin/env python3
"""Explicit first-time pins for an authorized disposable GitHub Actions job.
Does NOT install packages, edit repository defaults, or migrate saved manifests.
"""
import argparse,hashlib,json,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
DATA=ROOT/'cloud'/'mypaint_data'
LIBS={'libmypaint':('libmypaint-1.5-1',Path('/lib/x86_64-linux-gnu/libmypaint-1.5.so.1')),'json_c':('libjson-c5',Path('/lib/x86_64-linux-gnu/libjson-c.so.5'))}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def package_info(name,path,expected):
 result=subprocess.run(['dpkg-query','-W','-f=${Status}\t${Version}\t${Architecture}',name],check=True,text=True,capture_output=True).stdout.strip().split('\t')
 if len(result)!=3 or result[0]!='install ok installed':raise ValueError(f'{name} is not installed')
 status,version,architecture=result
 if version!=expected:raise ValueError(f'{name} version {version!r} does not match explicitly selected {expected!r}')
 files=subprocess.run(['dpkg-query','-L',name],check=True,text=True,capture_output=True).stdout.splitlines()
 if path.resolve() not in {Path(f).resolve() for f in files if f.startswith('/')}:raise ValueError(f'{path} is not owned by {name}')
 if not path.is_file():raise ValueError(f'{path} is unavailable')
 return {'package':name,'status':status,'version':version,'architecture':architecture,'library':str(path),'resolved_library':str(path.resolve()),'sha256':sha(path)}
def write_new(path,value):
 data=json.dumps(value,indent=2,sort_keys=True).encode()+b'\n'
 fd,tmp=tempfile.mkstemp(dir=path.parent,prefix='.mypaint-ci-')
 try:
  with os.fdopen(fd,'wb') as f:f.write(data)
  os.link(tmp,path) # Atomic no-clobber even if another file appeared after checks.
 finally:Path(tmp).unlink(missing_ok=True)
def provision(args):
 if os.environ.get('GITHUB_ACTIONS')!='true' or not args.allow_ci_provision:raise ValueError('Requires GITHUB_ACTIONS=true and explicit --allow-ci-provision')
 pins=Path(args.pins_file);receipt=Path(args.receipt_file)
 if not pins.is_absolute() or not receipt.is_absolute() or pins==receipt:raise ValueError('Use distinct absolute output paths')
 if pins.resolve()==(DATA/'pins.json').resolve() or receipt.resolve()==(DATA/'pins.json').resolve():raise ValueError('Repository default pins must not be changed')
 if pins.exists() or receipt.exists():raise FileExistsError('Refusing to update an existing pins or receipt file')
 expected={'libmypaint':args.expected_libmypaint_version,'json_c':args.expected_json_c_version}
 records={key:package_info(name,path,expected[key]) for key,(name,path) in LIBS.items()}
 payload={key:entry['sha256'] for key,entry in records.items()}
 payload.update(preset=sha(DATA/'pointy_ink.myb'),abi=sha(DATA/'abi15.py'))
 distro={}
 for line in Path('/etc/os-release').read_text().splitlines():
  if '=' in line and not line.startswith('#'):
   key,val=line.split('=',1);distro[key]=val.strip('"')
 evidence={'purpose':'Explicit authorized CI first-time configuration, not replay fallback','packages':records,'distribution':distro,'pins':payload,'github_run_id':os.environ.get('GITHUB_RUN_ID'),'dependency_scope':'Only libmypaint/json-c plus preset/ABI; not full dependency closure','installation':'No installation performed here; caller must use authorized distribution package sources'}
 write_new(pins,payload)
 # If second publication fails, retain clearly separate pins rather than silently
 # delete/replace a file another process may already be reading; caller must fail CI.
 write_new(receipt,evidence)
 return evidence
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--allow-ci-provision',action='store_true')
 parser.add_argument('--pins-file','--output',dest='pins_file',required=True);parser.add_argument('--receipt-file','--receipt',dest='receipt_file',required=True)
 parser.add_argument('--expected-libmypaint-version',required=True);parser.add_argument('--expected-json-c-version',required=True)
 print(json.dumps(provision(parser.parse_args()),indent=2))
