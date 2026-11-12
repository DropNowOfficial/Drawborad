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
# One manually drawn connected hair silhouette, sampled as short native strokes.
# Not threshold traced: vertices below are authored from visual observation.
poly=[(-25,-20),(294,-20),(267,20),(250,66),(266,113),(251,79),(219,86),(200,130),(200,181),(180,148),(170,75),(148,105),(120,213),(108,172),(87,111),(81,80),(38,53),(-25,77)]
for row in range(-10,212,2):
    xs=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        if min(a[1],b[1])<=row<max(a[1],b[1]):
            xs.append(a[0]+(row-a[1])*(b[0]-a[0])/(b[1]-a[1]))
    xs.sort()
    for x0,x1 in zip(xs[::2],xs[1::2]):
        curve(0,2.7,[(x0,row),(x0+(x1-x0)/3,row),(x0+2*(x1-x0)/3,row),(x1,row)],(1,1,1))
# Sparse intentional paper cuts, no noise texture.
curve(0,2.2,[(182,-5),(160,19),(150,38),(134,52)],(.02,.8,.02),239)
curve(0,1.4,[(94,-2),(74,13),(60,28),(42,36)],(.02,.8,.02),239)
curve(0,2.7,[(151,83),(145,107),(133,139),(124,169)],(.1,.9,.02),239)
# Right hair still limited to two broad directional marks; not claimed finished.
curve(0,22,[(602,-7),(594,75),(541,165),(461,231)],(.8,1,.02))
curve(0,28,[(627,34),(642,116),(611,170),(577,229)],(.8,1,.02))
# Brow ridges: startled raised angular curve; upper lid is strongest.
curve(1,7,[(237,114),(293,73),(393,73),(459,104)],(.03,.95,.35))
curve(1,3,[(455,103),(455,130),(442,166),(424,188)],(.75,1,.06))
curve(1,2.7,[(250,122),(281,151),(350,180),(425,203)],(.1,.8,.08))
curve(1,1.7,[(258,132),(298,164),(353,182),(411,211)],(.12,.7,.02))
curve(1,1.1,[(267,119),(304,152),(352,167),(405,190)],(.05,.75,.1))
curve(1,1.2,[(304,69),(357,68),(410,76),(477,102)],(.02,.6,.55))
curve(1,1.5,[(477,102),(479,111),(479,117),(478,126)],(.7,.8,.05))
curve(1,6,[(291,11),(375,14),(441,39),(518,95)],(.15,1,.12))
# True tilted ellipse: independent short chords of a parametric oval.
# Main axis tilted -24 degrees from vertical, not offset crescent curves.
angle=math.radians(-24)
def oval_point(u,v):
    return (405+u*math.cos(angle)+v*math.sin(angle),147-u*math.sin(angle)+v*math.cos(angle))
for v in range(-19,20):
    half=10.5*math.sqrt(max(0,1-(v/20)**2))
    a=oval_point(-half,v);b=oval_point(half,v)
    curve(2,1.5,[a,(a[0]+(b[0]-a[0])/3,a[1]+(b[1]-a[1])/3),(a[0]+2*(b[0]-a[0])/3,a[1]+2*(b[1]-a[1])/3),b],(1,1,1))
curve(2,3.1,[(410,134),(413,134),(410,139),(407,141)],(.6,1,.6),246)
curve(2,1.2,[(400,154),(399,155),(399,156),(398,156)],(.5,1,.5),246)
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
