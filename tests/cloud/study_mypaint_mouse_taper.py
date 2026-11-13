"""Technical stroke study, not artwork. Uses actual page transform + pinned worker."""
import json,subprocess,threading,sys
from pathlib import Path
from PIL import Image,ImageDraw
from cloud import mypaint_worker as w
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
html=Path('cloud/static/index.html').read_text();helper=html[html.index('function nativeMyPaintTaperPoints('):html.index('function nativeInkDraft(')]
sheet=Image.new('RGB',(1000,800),'white');draw=ImageDraw.Draw(sheet);receipts=[]
for row,(preset,pressure) in enumerate([(a,b) for a in ['pointy_ink','dry_brush'] for b in [.15,.35,.7]]):
 points=[dict(x=80+i*4.,y=150.,pressure=pressure,time_ms=i*12.) for i in range(81)]
 draw.text((10,row*130+5),f'{preset} pressure {pressure} | fixed left / taper right',fill='black')
 for col,fraction in enumerate([0,.2]):
  actual=json.loads(subprocess.check_output(['node','-e',helper+';console.log(JSON.stringify(nativeMyPaintTaperPoints('+json.dumps(points)+','+str(fraction)+')));']))
  d=out/f'{preset}-{pressure}-{fraction}';d.mkdir(exist_ok=True)
  body=dict(type='draw_stroke',brush='mypaint_ink',expected_revision='1',preset=preset,points=actual)
  png,manifest=w.render(body,dict(width='512',height='512',revision='1',document_id='a'*32,capabilities=['append_brush_crop']),d,threading.Event())
  m=json.loads(manifest.read_text());assert m['original_global_points']==actual
  canvas=Image.new('RGBA',(512,512),(255,255,255,255));im=Image.open(png).convert('RGBA');canvas.alpha_composite(im,tuple(m['origin']));view=canvas.crop((40,110,460,200)).convert('RGB');sheet.paste(view,(10+col*480,row*130+28))
  alpha=im.getchannel('A');bbox=alpha.getbbox();assert bbox
  receipts.append(dict(preset=preset,pressure=pressure,taper=fraction,bbox=bbox,visible_alpha=sum(alpha.getdata())))
sheet.save(out/'comparison.png');(out/'receipt.json').write_text(json.dumps(receipts,indent=2));print(json.dumps(receipts))
