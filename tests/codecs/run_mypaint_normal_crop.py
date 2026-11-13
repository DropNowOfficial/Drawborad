"""Run synthetic core/storage/renderer test in isolated directory and decode real ORA."""
from pathlib import Path
import tempfile,subprocess,sys,zipfile,io,xml.etree.ElementTree as ET
from PIL import Image
with tempfile.TemporaryDirectory(prefix='drawborad-normal-') as folder:
 subprocess.run([sys.argv[1],folder],check=True)
 with zipfile.ZipFile(Path(folder)/'normal.ora') as z:
  assert z.testzip() is None
  layers=ET.fromstring(z.read('stack.xml')).find('stack').findall('layer')
  assert layers[0].get('composite-op')=='svg:src-over'
  assert layers[1].get('composite-op')=='svg:multiply'
  assert layers[0].get('x')=='511' and layers[0].get('y')=='511'
  merged=Image.open(io.BytesIO(z.read('mergedimage.png'))).convert('RGBA')
  composite=Image.new('RGBA',(520,520),(0,0,0,255))
  # Lower white Multiply crop over black is an exact no-op. Upper normal alpha is independent oracle.
  top=Image.open(io.BytesIO(z.read(layers[0].get('src')))).convert('RGBA');composite.alpha_composite(top,(511,511))
  assert composite.tobytes()==merged.tobytes()
 print('PASS independent PNG/ZIP Normal alpha over black, mixed ORA modes and ROI')
