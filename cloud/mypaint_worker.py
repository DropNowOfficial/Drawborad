"""Optional, hash-pinned MyPaint worker. Never substitutes for native engines."""
import hashlib,json,math,os,subprocess,sys,time,tempfile,re
try:
 import resource
except ImportError:
 resource=None
from pathlib import Path
DATA=Path(__file__).with_name('mypaint_data')
PATHS={'libmypaint':Path('/lib/x86_64-linux-gnu/libmypaint-1.5.so.1'),'json_c':Path('/lib/x86_64-linux-gnu/libjson-c.so.5'),'preset':DATA/'pointy_ink.myb','abi':DATA/'abi15.py'}
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def dimensions(width,height):
 values=[]
 for value in (width,height):
  if type(value)==str and re.fullmatch(r'[1-9][0-9]{0,4}',value):value=int(value)
  if type(value)!=int or value<1:raise ValueError('Invalid native canvas dimensions')
  values.append(value)
 return tuple(values)
def available(width=None,height=None):
 if not sys.platform.startswith('linux') or resource is None:return False,'MyPaint experiment currently supports Linux workers only; existing brushes remain available.'
 try:
  if width is not None:width,height=dimensions(width,height)
  pins=json.loads((DATA/'pins.json').read_text())
  if any(sha(path)!=pins[key] for key,path in PATHS.items()):return False,'Pinned MyPaint runtime or preset differs; no automatic replacement.'
  import importlib.util
  if importlib.util.find_spec('PIL') is None:return False,'PNG encoder unavailable.'
  if width is not None and (type(width)!=int or type(height)!=int or min(width,height)<1 or max(width,height)>2048 or width*height>1048576):return False,'MyPaint requires ≤ 1,048,576 pixels and ≤ 2048 per axis; canvas is not resized.'
  return True,'Black pointy ink; preset pressure/size; each stroke is a separate locked Multiply layer.'
 except (OSError,ValueError,KeyError):return False,'Optional pinned MyPaint runtime is unavailable; no packages are installed automatically.'
def validate_request(body,state):
 if set(body)!={'type','brush','expected_revision','points'} or body.get('type')!='draw_stroke' or body.get('brush')!='mypaint_ink':raise ValueError('Invalid MyPaint fields; color/width/layer overrides are not supported.')
 revision=body['expected_revision']
 if not isinstance(revision,str) or revision!=state.get('revision'):raise ValueError('Stale MyPaint revision.')
 points=body['points'];w,h=dimensions(state['width'],state['height'])
 if not isinstance(points,list) or not 1<=len(points)<=1024:raise ValueError('MyPaint requires 1–1024 points.')
 last=-1
 for p in points:
  if not isinstance(p,dict) or set(p)!={'x','y','pressure','time_ms'}:raise ValueError('Invalid MyPaint sample fields.')
  if any(type(p[k]) not in (float,int) or not math.isfinite(p[k]) for k in p):raise ValueError('Nonfinite MyPaint sample.')
  if not (0<=p['x']<w and 0<=p['y']<h and 0<=p['pressure']<=1 and 0<=p['time_ms']<=60000 and last<=p['time_ms']):raise ValueError('MyPaint sample out of bounds.')
  last=p['time_ms']
 return points

def _limits():
 resource.setrlimit(resource.RLIMIT_AS,(256*1024*1024,256*1024*1024));resource.setrlimit(resource.RLIMIT_CPU,(10,10))
def render(body,state,folder,cancel):
 width,height=dimensions(state['width'],state['height']);state=dict(state,width=width,height=height)
 points=validate_request(body,state);ok,reason=available(width,height)
 if not ok:raise ValueError(reason)
 if cancel.is_set():raise RuntimeError('MyPaint cancelled; nothing added.')
 folder=Path(folder);request=folder/'request.json';png=folder/'stroke.png';manifest=folder/'manifest.json'
 record={'backend':'mypaint-pointy-ink-raster-v1','pins':json.loads((DATA/'pins.json').read_text()),'worker_sha256':sha(__file__),'canvas':[state['width'],state['height']],'color':'#000000','preset':'pointy_ink','width_policy':'original preset; pressure not equivalent to native width','encoding':'RGBA15 over opaque white -> RGBA8 nearest, no ICC transform','initial_zero_pressure_seconds':10,'points':points,'expected_revision':body['expected_revision'],'document_id':state.get('document_id')}
 request.write_text(json.dumps(record,allow_nan=False));proc=None
 try:
  proc=subprocess.Popen([sys.executable,__file__,'worker',str(request),str(png),str(manifest)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=dict(os.environ,OMP_NUM_THREADS='1'))
  deadline=time.monotonic()+15
  while proc.poll() is None:
   if cancel.is_set() or time.monotonic()>deadline:
    proc.kill();proc.communicate();raise RuntimeError('MyPaint cancelled or exceeded 15-second budget; nothing added.')
   time.sleep(.02)
  stdout,err=proc.communicate()
  if proc.returncode:raise RuntimeError('MyPaint worker failed; nothing added. '+err.decode(errors='replace')[-300:])
  result=json.loads(manifest.read_text())
  receipt=json.loads(stdout)
  if receipt['png_sha256']!=sha(png) or receipt['manifest_sha256']!=sha(manifest) or result['pins']!=record['pins']:raise RuntimeError('MyPaint output verification failed.')
  return png,manifest
 finally:
  if proc and proc.poll() is None:proc.kill();proc.communicate()

def worker(request,png,manifest):
 m=json.loads(Path(request).read_text());w,h=m['canvas'];ok,reason=available(w,h)
 if not ok or m['pins']!=json.loads((DATA/'pins.json').read_text()) or m['worker_sha256']!=sha(__file__):raise ValueError(reason or 'Worker pins mismatch')
 validate_request({'type':'draw_stroke','brush':'mypaint_ink','expected_revision':m['expected_revision'],'points':m['points']},{'width':w,'height':h,'revision':m['expected_revision']})
 import importlib.util
 spec=importlib.util.spec_from_file_location('mypaint_pinned_abi',DATA/'abi15.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
 from PIL import Image,PngImagePlugin
 surface=a.new(w,h);assert surface;iface=a.interface(surface);assert iface==surface;assert a.C.cast(surface,a.C.POINTER(a.Tiled)).contents.tile_size==64
 nx=(w+63)//64;ny=(h+63)//64
 for ty in range(ny):
  for tx in range(nx):
   r=a.Request();a.reqinit(a.C.byref(r),0,tx,ty,0);a.start(surface,a.C.byref(r))
   for i in range(64*64*4):r.buffer[i]=32768
   a.end(surface,a.C.byref(r))
 b=a.bnew();assert a.load(b,PATHS['preset'].read_bytes())==1
 for key in ['color_h','color_s','color_v']:a.setbase(b,a.setting(key.encode()),0)
 a.clear(b);a.reset(b);a.begin(iface);first=m['points'][0];a.stroke(b,iface,first['x'],first['y'],0,0,0,10.)
 last=first['time_ms']
 for p in m['points']:
  dt=max(.001,(p['time_ms']-last)/1000);a.stroke(b,iface,p['x'],p['y'],p['pressure'],0,0,dt);last=p['time_ms']
 a.stroke(b,iface,p['x'],p['y'],0,0,0,.02);roi=a.Rect();a.finish(iface,a.C.byref(roi));a.bfree(b)
 rgba=bytearray(w*h*4)
 for ty in range(ny):
  for tx in range(nx):
   r=a.Request();a.reqinit(a.C.byref(r),0,tx,ty,1);a.start(surface,a.C.byref(r))
   for y in range(min(64,h-ty*64)):
    for x in range(min(64,w-tx*64)):
     j=((ty*64+y)*w+tx*64+x)*4;k=(y*64+x)*4
     for c in range(4):rgba[j+c]=max(0,min(255,round(r.buffer[k+c]*255/32768)))
   a.end(surface,a.C.byref(r))
 a.sfree(iface);m['white_surface_rgba_sha256']=hashlib.sha256(rgba).hexdigest()
 for at in range(0,len(rgba),4):
  r,g,b,alpha=rgba[at:at+4]
  if r!=g or g!=b or alpha!=255:raise ValueError('Black ink worker requires opaque grayscale source')
  rgba[at:at+4]=bytes((0,0,0,255-r))
 m['rgba_sha256']=hashlib.sha256(rgba).hexdigest();m['alpha_conversion']='black RGB; alpha=255-gray; white recomposite exact'
 m['actual_input_policy']='fresh brush; dt=max(.001, delta_ms/1000); zero-pressure lift .02s'
 # No-clobber even inside the private temporary directory.
 fd,tmp=tempfile.mkstemp(suffix='.png',dir=Path(png).parent);os.close(fd)
 manifest_bytes=json.dumps(m,sort_keys=True,separators=(',',':'),allow_nan=False).encode();manifest_hash=hashlib.sha256(manifest_bytes).hexdigest()
 info=PngImagePlugin.PngInfo();info.add_text('DrawboradReplaySHA256',manifest_hash)
 try:
  Image.frombytes('RGBA',(w,h),bytes(rgba)).save(tmp,dpi=(300,300),pnginfo=info);os.link(tmp,png)
 finally:Path(tmp).unlink(missing_ok=True)
 with open(manifest,'xb') as f:f.write(manifest_bytes)
 print(json.dumps({'png_sha256':sha(png),'manifest_sha256':manifest_hash}))
if __name__=='__main__':
 _limits() # Set limits inside child; no unsafe preexec_fn in threaded FastAPI.
 worker(*sys.argv[2:])
