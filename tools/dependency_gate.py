"""Fail-closed development dependency gate. Verify never imports a network client."""
import argparse, hashlib, json, pathlib, sys

def verify(lock,cache,profile):
 errors=[]; root=pathlib.Path(cache).resolve()
 if lock.get('schema')!=1:return ['InvalidSchema']
 p=lock.get('profiles',{}).get(profile)
 if not isinstance(p,dict):return ['UnknownProfile']
 if p.get('status','qualified')!='qualified':errors.append('Unqualified')
 entries=p.get('artifacts',[]); seen=set()
 for a in entries:
  name=a.get('name','')
  if name in seen:errors.append('DuplicateArtifact')
  seen.add(name)
  digest=a.get('sha256','')
  if not isinstance(digest,str) or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):errors.append('MissingDigest');continue
  for k in ('version','license','abi','publisher_url','security_review'):
   if not isinstance(a.get(k),str) or not a[k].strip():errors.append('LicenseBlocked' if k=='license' else 'MissingMetadata')
  q=a.get('qualification',{})
  if q.get('status')!='qualified' or not q.get('evidence'):errors.append('Unqualified')
  path=a.get('path','')
  pp=pathlib.PurePosixPath(path)
  if not path or pp.is_absolute() or '..' in pp.parts or ':' in path or '\\' in path:errors.append('PathEscape');continue
  dest=(root/path).resolve()
  if not dest.is_relative_to(root):errors.append('PathEscape');continue
  if not dest.is_file():errors.append('MissingArtifact');continue
  try:
   h=hashlib.sha256()
   with dest.open('rb') as f:
    for b in iter(lambda:f.read(1<<20),b''):h.update(b)
   if h.hexdigest()!=digest:errors.append('DigestMismatch')
   if dest.stat().st_size!=a.get('size'):errors.append('SizeMismatch')
  except OSError:errors.append('IoError')
 for name in p.get('required',[]):
  if name not in seen:errors.append('MissingArtifact')
 if not p.get('required'):errors.append('EmptyProfile')
 return sorted(set(errors))

def acquire(lock,cache,profile):
 # Explicit developer operation only. No extraction or executable launch.
 import urllib.request
 p=lock['profiles'][profile];root=pathlib.Path(cache).resolve()
 if p.get('status')!='qualified':raise ValueError('Unqualified dependency manifest; acquisition disabled')
 for a in p['artifacts']:
  path=a['path'];pp=pathlib.PurePosixPath(path)
  if pp.is_absolute() or '..' in pp.parts or ':' in path or '\\' in path:raise ValueError('PathEscape')
  dest=(root/path).resolve()
  if not dest.is_relative_to(root):raise ValueError('PathEscape')
  if len(a.get('sha256',''))!=64:raise ValueError('MissingDigest')
  if not a['publisher_url'].startswith('https://'):raise ValueError('HTTPS required')
  if dest.exists():continue
  dest.parent.mkdir(parents=True,exist_ok=True);tmp=dest.with_name(dest.name+'.part')
  try:
   with urllib.request.urlopen(a['publisher_url'],timeout=60) as r,tmp.open('xb') as f:
    n=0;h=hashlib.sha256()
    while b:=r.read(1<<20):
     n+=len(b)
     if n>a['size']:raise ValueError('SizeMismatch')
     f.write(b);h.update(b)
   if n!=a['size'] or h.hexdigest()!=a['sha256']:raise ValueError('DigestMismatch')
   tmp.replace(dest)
  finally:tmp.unlink(missing_ok=True)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--mode',choices=['Acquire','Verify'],default='Verify');ap.add_argument('--cache',required=True);ap.add_argument('--lock',default=str(pathlib.Path(__file__).resolve().parents[1]/'dependencies/lock.json'));ap.add_argument('--profile',default='windows-x64');ns=ap.parse_args()
 try:
  lock=json.loads(pathlib.Path(ns.lock).read_text())
  if ns.mode=='Acquire':acquire(lock,ns.cache,ns.profile)
  errors=verify(lock,ns.cache,ns.profile)
 except (OSError,ValueError,KeyError,TypeError) as e:print('BLOCKED:',e);return 2
 if errors:print('BLOCKED: '+', '.join(errors));return 2
 print('PASS: local artifacts, digests, licenses, ABI and qualification records');return 0
if __name__=='__main__':sys.exit(main())
