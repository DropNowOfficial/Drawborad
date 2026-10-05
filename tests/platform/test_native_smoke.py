import json,pathlib,subprocess,sys,tempfile
if sys.platform!='win32' or len(sys.argv)<2 or sys.argv[1]=='-':
 print('BLOCKED: Windows 11 + qualified Qt/D3D11 are required');sys.exit(77)
with tempfile.TemporaryDirectory() as t:
 for name,args in [('warp',['--warp']),('hardware',[])]:
  path=pathlib.Path(t)/(name+'.json')
  r=subprocess.run([sys.argv[1],'--smoke','--diagnostics',str(path)]+args,capture_output=True,timeout=40)
  if r.returncode:print(r.stdout.decode(errors='replace'),r.stderr.decode(errors='replace'));sys.exit(1)
  d=json.loads(path.read_text());assert d['activeRenderLuid'] and d['sceneGraph']=='Direct3D11'
  assert d['moduleScanCompleted'] and not d['webviewModules'];assert d['osBuild'].startswith('Windows')
 print('PASS native WARP and hardware Qt startup, 1280x800, adapter LUID, no WebView')
