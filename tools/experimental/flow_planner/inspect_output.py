import argparse,json,math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('native',type=Path);p.add_argument('plan',type=Path);p.add_argument('source',type=Path);p.add_argument('width',type=int);p.add_argument('height',type=int);args=p.parse_args()
w,h=args.width,args.height
arr=lambda path:np.fromfile(path,np.uint8).reshape(h,w,4)
a=arr(args.native/'final.rgba');t=arr(args.source);f=arr(args.plan/'feedback.rgba')
report=dict(feedbackByteIdentical=bool(np.array_equal(a,f)),rgbVisibleMae=float(np.mean(abs(a[:,:,:3].astype(float)-t[:,:,:3])[t[:,:,3]>0])/255),alphaMae=float(np.mean(abs(a[:,:,3].astype(float)-t[:,:,3]))/255))
imgs=[(args.source,'source')]+[(args.native/(n+'.rgba'),n) for n in ['base','form','detail','final']]
sheet=Image.new('RGBA',(w*5,h+24),'white');d=ImageDraw.Draw(sheet)
for i,(path,label) in enumerate(imgs):
 im=Image.fromarray(arr(path));im.save(args.native/(label+'.png'));sheet.alpha_composite(im,(i*w,24));d.text((i*w+5,4),label,fill='black')
sheet.convert('RGB').save(args.native/'stages.png')
meta=json.loads((args.native/'audit.json').read_text())
for phase in ['base','form','detail']:
 lengths=[sum(math.hypot(b['x']-a['x'],b['y']-a['y']) for a,b in zip(s['geometry'],s['geometry'][1:])) for s in meta['events'] if s['phase']==phase]
 report[phase]=dict(events=len(lengths),short=sum(n<3 for n in lengths),median=float(np.median(lengths)))
(args.native/'self-check.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
