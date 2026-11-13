"""Native 11x17 inch, 300 DPI layered document; no image upscaling."""
import io,json,subprocess,sys,tempfile,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image
with tempfile.TemporaryDirectory() as d:
 root=Path(d);proc=subprocess.Popen([str(Path(sys.argv[1]).resolve()),'session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);seq=0
 def ok(method,**params):
  global seq
  seq+=1;proc.stdin.write(json.dumps(dict(request_id=str(seq),method=method,params=params))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['request_id']==str(seq) and 'result' in r,r;return r['result']
 try:
  state=ok('create',width='3300',height='5100');assert state['ppi']==300
  for y,width,color in [(5000.5,.25,'#000000'),(100.5,2,'#ff0000')]:
   old={n['id'] for n in state['nodes']};state=ok('command',type='add_fill_layer',expected_revision=state['revision']);layer=next(n['id'] for n in state['nodes'] if n['id'] not in old and n['kind']==3)
   state=ok('command',type='draw_stroke',layer_id=layer,points=[{'x':100,'y':y},{'x':200,'y':y}],width=width,color=color,expected_revision=state['revision'])
  ok('export',path=str(root/'poster.png'),format='png')
  with Image.open(root/'poster.png') as image:
   assert image.size==(3300,5100);assert all(abs(v-300)<.05 for v in image.info['dpi'])
   assert image.getpixel((150,5000))==(0,0,0,64);assert image.getpixel((150,4998))==(0,0,0,0)
  ok('export',path=str(root/'poster.ora'),format='ora')
  with zipfile.ZipFile(root/'poster.ora') as z:
   layers=ET.fromstring(z.read('stack.xml')).findall('.//layer');assert len(layers)==2
   seen=[]
   for layer in layers:
    with Image.open(io.BytesIO(z.read(layer.get('src')))) as img:
     assert img.size==(3300,5100);seen.append(img.getpixel((150,5000)))
   assert (0,0,0,64) in seen and (0,0,0,0) in seen
  print('3300x5100 / 300DPI native fine strokes and separate ORA leaves passed')
 finally:
  proc.stdin.close();proc.wait(timeout=10)
