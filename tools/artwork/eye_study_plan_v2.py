"""Manually authored Bezier local eye study. No reference pixels are read.
Coordinate design space follows the observed crop proportions; curves are sampled
in the actual 1600x1000 canvas. This is not an automatic tracing algorithm.
"""
import json,math,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
strokes=[]
def curve(layer,width,points,pressure=(.12,.95,.12),gray=28):
    # points = p0 + three control/end points per cubic.
    samples=[]
    for j in range(1,len(points),3):
        a,b,c,d=points[j-1:j+3]
        for k in range(193):
            t=k/192;u=1-t
            x=u**3*a[0]+3*u*u*t*b[0]+3*u*t*t*c[0]+t**3*d[0]
            y=u**3*a[1]+3*u*u*t*b[1]+3*u*t*t*c[1]+t**3*d[1]
            phase=(len(samples))/(192*((len(points)-1)//3))
            p=pressure[0]+(pressure[1]-pressure[0])*min(1,phase*3) if phase<.5 else pressure[2]+(pressure[1]-pressure[2])*min(1,(1-phase)*3)
            samples.append([80+x*2.3,100+y*2.3,max(.02,min(1,p))])
    strokes.append([layer,width*2.3,gray,samples])
# Heavy hair as separate tapered directional strokes, rather than all fine lines.
for pts,w in [([(5,5),(70,-20),(130,0),(115,175)],37), ([(75,0),(122,-10),(175,-15),(178,150)],32), ([(128,0),(163,-9),(220,-10),(210,75)],27), ([(192,1),(223,-15),(267,-10),(265,102)],12), ([(550,-6),(536,36),(518,55),(495,90)],18), ([(603,-7),(600,80),(553,172),(461,231)],30), ([(627,34),(642,116),(611,170),(577,229)],31), ([(625,123),(637,172),(620,197),(620,244)],17)]:
    curve(0,w,pts,(.8,1,.04))
# Directional dry ink accents; deliberate separated pale slivers, not random noise.
for a,b,c,d in [( (90,3),(83,65),(109,141),(117,206)),((167,20),(163,78),(171,164),(199,182)),((246,0),(225,34),(213,69),(211,103)),((284,-9),(275,25),(262,66),(264,115)),((593,1),(584,59),(564,121),(519,166)),((579,45),(556,104),(504,176),(452,228))]:curve(0,4,[a,b,c,d],(.2,.9,.03))
# Brow ridges: startled raised angular curve; upper lid is strongest.
curve(1,7,[(242,111),(304,76),(391,81),(459,108)],(.03,.95,.35))
curve(1,3,[(458,108),(462,129),(457,151),(426,183)],(.75,1,.06))
curve(1,2.7,[(253,122),(292,153),(354,177),(420,202)],(.1,.8,.08))
curve(1,1.7,[(258,132),(298,164),(353,182),(411,211)],(.12,.7,.02))
curve(1,1.1,[(267,119),(304,152),(352,167),(405,190)],(.05,.75,.1))
curve(1,1.2,[(304,69),(357,68),(410,76),(477,102)],(.02,.6,.55))
curve(1,1.5,[(477,102),(479,111),(479,117),(478,126)],(.7,.8,.05))
curve(1,6,[(291,11),(375,14),(441,39),(518,95)],(.15,1,.12))
# Tilted pupil: multiple curved marks form a compact organic oval, tiny gaps for glint.
for dx in range(-8,9):curve(2,2.8,[(408+dx,130),(419+dx,132),(407+dx,159),(399+dx,161)],(.5,1,.5))
curve(2,2,[(407,133),(410,132),(412,134),(409,139)],(.7,1,.7),239)
curve(2,1.3,[(402,139),(402,141),(400,143),(399,144)],(.7,1,.2),239)
# Ear and cheek frame, sweat bead; lineweight varies rather than technical outlines.
curve(3,2.4,[(3,110),(18,88),(26,86),(39,83)],(.25,.9,.08))
curve(3,2.1,[(42,90),(51,97),(52,108),(59,116)],(.5,.8,.1))
curve(3,2.7,[(24,183),(29,159),(56,147),(75,145)],(.2,1,.25))
curve(3,2.1,[(75,143),(76,155),(80,164),(85,169)],(.2,.8,.4))
curve(3,3.1,[(85,169),(74,191),(61,203),(65,216)],(.5,1,.65))
curve(3,1.8,[(65,216),(72,225),(78,236),(76,243)],(.5,.9,.15))
curve(3,2.4,[(76,243),(60,233),(39,234),(29,223)],(.2,.9,.2))
curve(3,1.4,[(169,185),(167,211),(154,248),(155,264),(161,277),(169,265),(173,211)],(.04,.9,.04))
# Short nonuniform facial tension hatch, authored individually.
for x,y,dx,dy in [(211,213,62,-49),(222,219,58,-43),(237,224,56,-38),(254,230,48,-31),(276,229,36,-24),(293,233,37,-24)]:curve(3,.9,[(x,y),(x+dx*.25,y+dy*.2),(x+dx*.7,y+dy*.8),(x+dx,y+dy)],(.04,.7,.03),48)
(out/'eye-plan.json').write_text(json.dumps({'size':[1600,1000],'layers':['Directional hair masses','Raised brow and eye contour','Pupil and highlights','Face and tension marks'],'strokes':strokes}))
with (out/'eye-plan.txt').open('w') as f:
 f.write(str(len(strokes))+'\n')
 for layer,width,gray,ps in strokes:
  f.write(f'{layer} {width} {gray} {len(ps)}\n')
  for x,y,p in ps:f.write(f'{x} {y} {p}\n')
print(len(strokes),'manually authored curves; native output 1600 x 1000')
