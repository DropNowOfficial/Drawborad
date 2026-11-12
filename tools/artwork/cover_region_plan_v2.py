"""Reference-driven native color-region trace; not hand redrawing or upscaling.
No source image is embedded in the resulting native document. Preserve topology
and broad/thin mark hierarchy, rather than skeletonizing everything to fine lines.
"""
import json
from pathlib import Path
import sys
from PIL import Image, ImageDraw
source=Path(sys.argv[1]);output=Path(sys.argv[2]);output.mkdir(parents=True,exist_ok=False)
im=Image.open(source).convert('RGB');w,h=im.size
assert (w,h)==(1024,1536),'This reviewed semantic layout is only for the black approved cover.'
names=['Framing dry black field','Upper face and rough hair','What sister speech','Photo boy and missing sister','Father face fine marks','Hand and finger contour','Title THE LAST FRAME','Bottom right darkroom','Missing sister paper silhouette','DELETE red accents']
regions=Image.new('L',(w,h));d=ImageDraw.Draw(regions)
d.rectangle((0,0,715,345),fill=1);d.ellipse((650,74,1023,350),fill=2)
d.polygon([(18,373),(640,271),(796,753),(132,899)],fill=3)
d.polygon([(760,345),(1023,325),(1023,776),(785,714)],fill=4)
d.polygon([(788,739),(1023,803),(1023,1130),(797,1194),(667,1107),(686,995),(604,967),(588,900)],fill=5)
d.polygon([(0,980),(555,1004),(565,1517),(0,1535)],fill=6)
d.polygon([(569,1250),(1023,1098),(1023,1535),(550,1535)],fill=7)
# The blank person's existing silhouette remains an opaque paper region, not
# a newly inferred person. This polygon is only semantic assignment, not geometry.
d.polygon([(431,486),(454,443),(509,416),(553,431),(584,461),(597,500),(615,540),(599,574),(621,629),(663,670),(698,708),(732,729),(370,801),(391,746),(418,687),(427,631),(416,589)],fill=8)
labels=list(regions.getdata());pixels=list(im.getdata())
palette=[(24,24,23),(42,41,39),(58,56,53),(76,73,70),(96,92,88),(121,116,111),(148,142,137),(177,169,161),(204,195,185),(226,217,205),(246,239,225),(121,53,47),(151,66,58),(180,78,67),(206,103,88),(226,148,126)]
thresholds=[35,50,67,85,107,134,163,190,215,235]
classified=[]
for i,(r,g,b) in enumerate(pixels):
 red=r-g>32 and r>b*1.18 and r>90
 if red:col=11 if r<140 else (12 if r<168 else (13 if r<194 else (14 if r<220 else 15)));region=9
 else:
  lum=(r*299+g*587+b*114)//1000
  col=sum(lum>=threshold for threshold in thresholds)
  region=labels[i]
 if col==10 and region!=8:classified.append(None)
 else:classified.append((region,col))
runs={}
for y in range(h):
 x=0
 while x<w:
  key=classified[y*w+x];start=x;x+=1
  while x<w and classified[y*w+x]==key:x+=1
  if key is not None:runs.setdefault(key,[]).append((y,start,x))
chunks=[]
for key,spans in sorted(runs.items()):
 for i in range(0,len(spans),60000):chunks.append((key,spans[i:i+60000]))
points=sum(len(spans)*2 for _,spans in chunks)
assert points<=1000000,(points,'native geometry budget')
with (output/'plan.txt').open('w') as f:
 f.write(f'COVERREGIONS1 {w} {h} {len(names)} {len(chunks)}\n')
 for n in names:f.write(n.replace(' ','_')+'\n')
 for (region,col),spans in chunks:
  f.write(f'{region} {palette[col][0]} {palette[col][1]} {palette[col][2]} {len(spans)}\n')
  for y,x0,x1 in spans:f.write(f'{y} {x0} {x1}\n')
(output/'plan-receipt.json').write_text(json.dumps({'source':source.name,'size':[w,h],'layers':names,'events':len(chunks),'geometry_points':points,'method':'Quantized native region tracing, not hand redraw; no model or embedded source image','limitations':['Original paper grain removed','Sixteen-color quantization loses subtle gray/color texture','Semantic region separation approximate','Original 1024x1536 detail ceiling, not high-resolution restoration']},indent=2)+'\n')
print('events',len(chunks),'points',points)
