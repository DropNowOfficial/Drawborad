#!/usr/bin/env python3
"""Native-feedback multiscale flow research, not semantic painting inference."""
import argparse
import ctypes as C
import json
import math
from pathlib import Path
import time
import numpy as np
from scipy import ndimage as ndi
from geometry import pigment_paths
from ridge import ridge_field, trace_ridge

PHASES = ('base','form','detail')
MAX_EVENTS, MAX_SAMPLES = 8192, 500000

class NativeCanvas:
    """Checked Python owner for a bounded native feedback canvas.

    Returned pixel arrays own their memory. Closing a canvas cannot leave a
    caller holding a NumPy view into freed C++ storage.
    """
    def __init__(self, rgba):
        self.handle=None;self._candidate_ready=False;self._pending=False
        if not isinstance(rgba,np.ndarray) or rgba.dtype!=np.uint8 or rgba.ndim!=3 or rgba.shape[2]!=4 or not all(1<=n<=512 for n in rgba.shape[:2]):
            raise ValueError('native feedback requires positive <=512 RGBA8 array')
        rgba=np.ascontiguousarray(rgba);self.h,self.w=rgba.shape[:2]
        self.lib=C.CDLL(str(Path(__file__).parent/'build/libflowfeedback.so'))
        u8=C.POINTER(C.c_uint8);f64=C.POINTER(C.c_double)
        self.lib.flow_create.argtypes=[C.c_uint,C.c_uint,u8,C.c_size_t];self.lib.flow_create.restype=C.c_void_p
        self.lib.flow_destroy.argtypes=[C.c_void_p]
        self.lib.flow_phase.argtypes=[C.c_void_p];self.lib.flow_phase.restype=C.c_int
        self.lib.flow_gradient_weight.argtypes=[C.c_void_p,C.c_double];self.lib.flow_gradient_weight.restype=C.c_int
        self.gradient_weight=0.
        self.lib.flow_pixels.argtypes=[C.c_void_p];self.lib.flow_pixels.restype=u8
        self.lib.flow_candidate.argtypes=[C.c_void_p,C.c_double,C.c_double,C.c_uint,f64,C.c_size_t,u8,C.c_size_t,C.POINTER(C.c_int)]
        self.lib.flow_coverage.argtypes=[C.c_void_p];self.lib.flow_coverage.restype=C.POINTER(C.c_uint16)
        self.lib.flow_evaluate.argtypes=[C.c_void_p,f64,C.c_size_t,C.c_double];self.lib.flow_evaluate.restype=C.c_double
        self.lib.flow_commit.argtypes=[C.c_void_p];self.lib.flow_commit.restype=C.c_int
        self.handle=self.lib.flow_create(self.w,self.h,rgba.ctypes.data_as(u8),rgba.size)
        if not self.handle:raise ValueError('native canvas creation failed')
        self._view=np.ctypeslib.as_array(self.lib.flow_pixels(self.handle),shape=(self.h*self.w*4,)).reshape(self.h,self.w,4)
    def _open(self):
        if not self.handle:raise ValueError('native canvas is closed')
    @property
    def pixels(self):
        self._open();return self._view.copy()
    def region(self,box):
        self._open();return self._view[box].copy()
    def phase(self):
        self._open();self._pending=False;self._candidate_ready=False
        if self.lib.flow_phase(self.handle):raise ValueError('native phase rejected')
    def gradients(self,weight):
        self._open();self._pending=False
        if not np.isfinite(weight) or not 0<=weight<=2 or self.lib.flow_gradient_weight(self.handle,weight):raise ValueError('invalid gradient weight')
        self.gradient_weight=weight
    def candidate(self,stroke,mask):
        self._open();self._pending=False;self._candidate_ready=False
        if not isinstance(mask,np.ndarray) or mask.dtype!=np.uint8 or mask.shape!=(self.h,self.w) or not np.all((mask==0)|(mask==255)):
            raise ValueError('native feedback requires same-sized binary U8 mask')
        mask=np.ascontiguousarray(mask)
        points=np.ascontiguousarray(stroke['geometry'],np.float64)
        width,hardness=float(stroke['width']),float(stroke['hardness'])
        if points.ndim!=2 or points.shape[1]!=3 or not 1<=len(points)<=500000 or not np.isfinite(points).all() or not np.isfinite(width) or not 0<width<=128 or not np.isfinite(hardness) or not 0<=hardness<=1:
            raise ValueError('native geometry/brush budget')
        if np.any((points[:,0]<0)|(points[:,0]>=self.w)|(points[:,1]<0)|(points[:,1]>=self.h)|(points[:,2]<0)|(points[:,2]>1)):
            raise ValueError('native sample outside bounds')
        bounds=(C.c_int*4)()
        code=self.lib.flow_candidate(self.handle,width,hardness,len(points),points.ctypes.data_as(C.POINTER(C.c_double)),points.size,mask.ctypes.data_as(C.POINTER(C.c_uint8)),mask.size,bounds)
        if code:raise ValueError(f'native coverage rejected {code}')
        self._candidate_ready=True;x,y,w,h=bounds
        coverage=np.ctypeslib.as_array(self.lib.flow_coverage(self.handle),shape=(w*h,)).reshape(h,w).astype(float)/65535
        return (slice(y,y+h),slice(x,x+w)),coverage
    def evaluate(self,color,opacity):
        self._open();self._pending=False
        if not self._candidate_ready:raise ValueError('candidate required before evaluation')
        color=np.ascontiguousarray(color,np.float64)
        if color.shape!=(4,) or not np.isfinite(color).all() or np.any((color<0)|(color>1)) or not np.isfinite(opacity) or not 0<=opacity<=1:
            raise ValueError('RGBA color must contain exactly four finite unit channels')
        gain=self.lib.flow_evaluate(self.handle,color.ctypes.data_as(C.POINTER(C.c_double)),color.size,opacity)
        if not np.isfinite(gain):raise ValueError('native evaluation state/input rejected')
        self._pending=gain>-1e29
        return gain
    def commit(self):
        self._open()
        if not self._pending or self.lib.flow_commit(self.handle):raise ValueError('successful evaluation required before commit')
        self._pending=False;self._candidate_ready=False
    def close(self):
        if self.handle:self.lib.flow_destroy(self.handle);self.handle=None
        self._view=None;self._pending=False;self._candidate_ready=False
    def __del__(self):self.close()


def tensor(source, width):
    sigma=max(.55,width*.18)
    rgb=ndi.gaussian_filter(source[:,:,:3],(sigma,sigma,0),mode='nearest')
    gx=ndi.sobel(rgb,axis=1,mode='nearest')/8
    gy=ndi.sobel(rgb,axis=0,mode='nearest')/8
    rho=max(.65,width*.2)
    xx=ndi.gaussian_filter((gx*gx).sum(2),rho)
    xy=ndi.gaussian_filter((gx*gy).sum(2),rho)
    yy=ndi.gaussian_filter((gy*gy).sum(2),rho)
    angle=.5*np.arctan2(2*xy,xx-yy)+np.pi/2
    coherence=np.sqrt((xx-yy)**2+4*xy**2)/(xx+yy+1e-12)
    # Flat fields have no preferred visual direction. Keep one stable direction
    # only for smooth broad washes, not invented detailed texture.
    angle=np.where(xx+yy<1e-9,0,angle)
    return rgb,np.cos(angle),np.sin(angle),coherence


def trace(seed, width, field, mask, max_length):
    rgb,vx,vy,coherence=field;h,w=mask.shape;x,y=seed
    color=rgb[int(y),int(x)]
    step=max(.55,min(2.5,width*.3))
    def valid(a,b):
        n=max(2,int(math.hypot(b[0]-a[0],b[1]-a[1])*4)+2)
        for t in np.linspace(0,1,n):
            px=a[0]+(b[0]-a[0])*t;py=a[1]+(b[1]-a[1])*t
            if px<0 or py<0 or px>=w or py>=h or not mask[int(py),int(px)]:return False
        return True
    branches=[]
    for sign in (-1,1):
        path=[(x,y)];dx=sign*float(vx[int(y),int(x)]);dy=sign*float(vy[int(y),int(x)])
        for _ in range(int(max_length/step/2)):
            px,py=path[-1];ix,iy=int(px),int(py)
            nx,ny=float(vx[iy,ix]),float(vy[iy,ix])
            if nx*dx+ny*dy<0:nx,ny=-nx,-ny
            nx,ny=.75*nx+.25*dx,.75*ny+.25*dy
            norm=math.hypot(nx,ny)
            nx,ny=nx/norm,ny/norm
            q=(px+step*nx,py+step*ny)
            if not valid(path[-1],q):break
            # Preserve actual local color variation; do not grow a constant-color
            # stroke through an unrelated hue just to inflate path length.
            if np.max(np.abs(rgb[int(q[1]),int(q[0])]-color))>max(.08,min(.3,width*.014)):
                break
            if len(path)>5 and math.hypot(q[0]-x,q[1]-y)<step*1.5:break
            path.append(q);dx,dy=nx,ny
        branches.append(path)
    path=list(reversed(branches[0][1:]))+branches[1]
    length=sum(math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(path,path[1:]))
    if length<max(3.,min(10.,width*.75)):return None
    # Taper fine structural paths only. Wide washes need stable support.
    n=len(path)
    return [(px,py,1. if width>=4 else .6+.4*min(1.,min(i,n-1-i)/2)) for i,(px,py) in enumerate(path)]


def error(target,current):
    t=target.astype(float)/255;c=current.astype(float)/255
    black=t[:,:,:3]*t[:,:,3:4]-c[:,:,:3]*c[:,:,3:4]
    white=black+c[:,:,3:4]-t[:,:,3:4]
    return np.sqrt(.5*np.sum(black*black+white*white,2)+(t[:,:,3]-c[:,:,3])**2)


def alpha_limit(target,current,coverage):
    # A forward-only paint planner cannot undo excess alpha. Limit candidate
    # opacity from its actual whole-stroke coverage, never multiply coverage by
    # the reference alpha image or persist that image as a mask.
    ta=target[:,:,3].astype(float)/255;ca=current[:,:,3].astype(float)/255
    basis=coverage*(1-ca)
    eligible=(basis>1e-9)&(ta<1)
    if not eligible.any():return 1.
    allowance=np.where(ta>0,1/255,0)
    return float(np.clip(np.min((ta-ca+allowance)[eligible]/basis[eligible]),0,1))


def fit(canvas,target,stroke,mask,min_gain):
    box,coverage=canvas.candidate(stroke,mask)
    if not coverage.any():return False
    t=target[box].astype(float)/255;c=canvas.region(box).astype(float)/255
    tc=t[:,:,:3]*t[:,:,3:4];cc=c[:,:,:3]*c[:,:,3:4]
    alpha_basis=coverage*(1-c[:,:,3]);den=np.sum(alpha_basis*alpha_basis)
    limit=alpha_limit(target[box],canvas.region(box),coverage)
    if limit<1e-5:return False
    opacity=[limit,limit*.55]
    if den>1e-8:
        optimum=float(np.clip(np.sum(alpha_basis*(t[:,:,3]-c[:,:,3]))/den,1e-5,limit))
        opacity=sorted(set([optimum,min(limit,optimum*1.25),optimum*.75]),reverse=True)
    best=None
    for op in opacity:
        a=coverage*op;denom=np.sum(a*a)
        if denom<1e-8:continue
        numerator=np.sum(a[:,:,None]*(tc-(1-a[:,:,None])*cc),axis=(0,1))
        if canvas.gradient_weight:
            y,x=box;y0,y1,x0,x1=y.start,y.stop,x.start,x.stop
            def extended(image):
                tile=image[max(0,y0-1):min(canvas.h,y1+1),max(0,x0-1):min(canvas.w,x1+1)].astype(float)/255
                pad=((int(y0==0),int(y1==canvas.h)),(int(x0==0),int(x1==canvas.w)),(0,0))
                return np.pad(tile,pad,mode='edge')
            target_ext,current_ext=extended(target),extended(canvas.pixels)
            target_ext=target_ext[:,:,:3]*target_ext[:,:,3:4];current_ext=current_ext[:,:,:3]*current_ext[:,:,3:4]
            alpha_ext=np.pad(a,1)
            for axis in (0,1):
                da=np.diff(alpha_ext,axis=axis)
                residual=np.diff(target_ext-(1-alpha_ext[:,:,None])*current_ext,axis=axis)
                numerator+=canvas.gradient_weight*np.sum(da[:,:,None]*residual,axis=(0,1))
                denom+=canvas.gradient_weight*np.sum(da*da)
        pigment=np.clip(numerator/denom,0,1)
        color=[*pigment,1.]
        gain=canvas.evaluate(color,op)
        if best is None or gain>best[0]:best=(gain,color,op)
    if best is None or best[0]<min_gain:return False
    canvas.evaluate(best[1],best[2]);canvas.commit()
    stroke.update(color=list(map(float,best[1])),opacity=float(best[2]),gain=float(best[0]))
    return True


def plan(rgba,masks,allow_soft_alpha=False):
    h,w=rgba.shape[:2]
    if rgba.dtype!=np.uint8 or rgba.shape!=(h,w,4) or not 1<=w<=512 or not 1<=h<=512:raise ValueError('RGBA8 input budget')
    if not 1<=len(masks)<=16:raise ValueError('object count')
    claimed=np.zeros((h,w),bool)
    for m in masks:
        if m.dtype!=np.uint8 or m.shape!=(h,w) or not np.isin(m,[0,255]).all() or not m.any():raise ValueError('binary mask')
        if np.any(claimed&(m>0)):raise ValueError('overlapping masks')
        claimed|=m>0
    if not allow_soft_alpha and np.any(rgba[:,:,3][claimed]!=255):
        raise ValueError('unsupported soft alpha in reviewed pixels; no plan generated (use explicit diagnostic mode only for negative testing)')
    target=rgba.copy();target[:,:,3]*=claimed
    canvas=NativeCanvas(target);strokes=[];total_samples=0;stats=[]
    def accept(stroke):
        nonlocal total_samples
        if len(strokes)>=MAX_EVENTS or total_samples+len(stroke['geometry'])>MAX_SAMPLES:raise RuntimeError('research plan budget exceeded')
        strokes.append(stroke);total_samples+=len(stroke['geometry'])
    source=rgba.astype(float)/255
    # Geometric underpainting is genuine long native contour strokes, not a fill.
    for oi,mask in enumerate(masks):
        opaque=(mask>0)&(rgba[:,:,3]==255)
        if not opaque.any():continue
        color=np.r_[np.mean(source[:,:,:3][opaque],axis=0),1.]
        for stroke in pigment_paths(opaque,color,max_width=64,boundary_padding=2 if np.all(opaque[mask>0]) else 0):
            stroke.update(object=oi,phase='base',hardness=1.,opacity=1.)
            box,coverage=canvas.candidate(stroke,mask)
            stroke['opacity']=alpha_limit(target[box],canvas.region(box),coverage)
            if stroke['opacity']>1e-5 and canvas.evaluate(stroke['color'],stroke['opacity'])>-1e20:
                canvas.commit();accept(stroke)
    schedule=[('base',32.,.0,2,512),('base',16.,.0,2,1024),('form',8.,.1,2,1024),('form',4.,.35,2,1024),('detail',2.,.75,2,1536),('detail',1.2,1.,1,1024)]
    previous='base'
    for phase,width,hardness,passes,level_budget in schedule:
        if phase!=previous:
            canvas.phase();previous=phase
            canvas.gradients(.85 if phase=='detail' else 0.)
        field=tensor(source,width);count_before=len(strokes);start=time.monotonic()
        for repeat in range(passes):
            errors=error(target,canvas.pixels)
            spacing=max(2,int(width*.65));offset=(repeat*spacing//2)
            candidates=[]
            for oi,mask in enumerate(masks):
                for y in range(-offset,h,spacing):
                    for x in range(-offset,w,spacing):
                        y0,x0=max(0,y),max(0,x);y1,x1=min(h,y+spacing),min(w,x+spacing)
                        if y1<=y0 or x1<=x0:continue
                        local=errors[y0:y1,x0:x1]*(mask[y0:y1,x0:x1]>0)
                        iy,ix=np.unravel_index(local.argmax(),local.shape);value=float(local[iy,ix])
                        if value>.018:candidates.append((value,oi,x0+ix+.5,y0+iy+.5))
            candidates.sort(key=lambda v:(-v[0],v[1],v[3],v[2]))
            for value,oi,x,y in candidates:
                if len(strokes)-count_before>=level_budget:break
                current=canvas.region((slice(int(y),int(y)+1),slice(int(x),int(x)+1)))
                if error(target[int(y):int(y)+1,int(x):int(x)+1],current)[0,0]<.018:continue
                path=trace((x,y),width,field,masks[oi],min(100,width*9))
                if path is None:continue
                stroke=dict(object=oi,phase=phase,width=width,hardness=hardness,geometry=path)
                if fit(canvas,target,stroke,masks[oi],max(.004,width*width*.00012)):
                    accept(stroke)
        stats.append(dict(phase=phase,width=width,events=len(strokes)-count_before,seconds=time.monotonic()-start,eventLimit=level_budget,eventLimitReached=len(strokes)-count_before>=level_budget))
        print(json.dumps(stats[-1]),flush=True)
    # Fine texture is a line-center problem, not a second pair of edge strokes.
    # Use evidenced bright/dark Hessian ridges with consistent polarity. Every
    # segment still stays inside the reviewed binary mask and must improve the
    # actual native rendering before it can become a brush event.
    ridges=ridge_field(source)
    errors=error(target,canvas.pixels);scores=errors*ridges['strength'];candidates=[]
    for oi,mask in enumerate(masks):
        for y in range(0,h,2):
            for x in range(0,w,2):
                local=scores[y:y+2,x:x+2]*(mask[y:y+2,x:x+2]>0)
                iy,ix=np.unravel_index(local.argmax(),local.shape)
                value=float(local[iy,ix])
                if value>.00025:candidates.append((value,oi,x+ix+.5,y+iy+.5))
    candidates.sort(key=lambda v:(-v[0],v[1],v[3],v[2]))
    count_before=len(strokes);start=time.monotonic();limit=min(1792,MAX_EVENTS-len(strokes))
    for value,oi,x,y in candidates:
        if len(strokes)-count_before>=limit:break
        if error(target[int(y):int(y)+1,int(x):int(x)+1],canvas.region((slice(int(y),int(y)+1),slice(int(x),int(x)+1))))[0,0]<.018:continue
        path=trace_ridge((x,y),ridges,masks[oi],32)
        if path is None or total_samples+len(path)>MAX_SAMPLES:continue
        stroke=dict(object=oi,phase='detail',width=.85,hardness=1.,geometry=path,geometrySource='gaussian-line-center')
        if fit(canvas,target,stroke,masks[oi],.003):accept(stroke)
    stats.append(dict(phase='detail',width=.85,geometrySource='gaussian-line-center',events=len(strokes)-count_before,
                      seconds=time.monotonic()-start,eventLimit=limit,eventLimitReached=len(strokes)-count_before>=limit))
    print(json.dumps(stats[-1]),flush=True)
    return dict(schema='DBFLOW1',width=w,height=h,objects=len(masks),strokes=strokes,statistics=stats,
                budget=dict(maxEvents=MAX_EVENTS,maxSamples=MAX_SAMPLES,maxNib=128),
                inputStatus='diagnostic-soft-alpha' if np.any(rgba[:,:,3][claimed]!=255) else 'opaque-research',
                qualityStatus='not-evaluated',naturalProcessVerified=False),canvas.pixels.copy()


def write_plan(result,output):
    output.mkdir(parents=True,exist_ok=False)
    (output/'plan.json').write_text(json.dumps(result,separators=(',',':')))
    with (output/'plan.txt').open('w') as f:
        f.write(f"DBFLOW1 {result['width']} {result['height']} {result['objects']} {len(result['strokes'])}\n")
        for s in result['strokes']:
            f.write(f"{s['object']} {PHASES.index(s['phase'])} {s['width']:.17g} {s['hardness']:.17g} {s['opacity']:.17g} "+' '.join(f'{v:.17g}' for v in s['color'])+f" {len(s['geometry'])}\n")
            for x,y,p in s['geometry']:f.write(f'{x:.17g} {y:.17g} {p:.17g}\n')


def exact(path,size):
    if not path.is_file() or path.stat().st_size!=size:raise ValueError('expected input byte size')
    with path.open('rb') as f:data=f.read(size+1)
    if len(data)!=size:raise ValueError('input changed during read')
    return data


def main():
    p=argparse.ArgumentParser();p.add_argument('--rgba',type=Path,required=True);p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True);p.add_argument('--mask',type=Path,action='append',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--research',action='store_true');p.add_argument('--diagnostic-soft-alpha',action='store_true')
    args=p.parse_args()
    if not args.research:p.error('explicit --research required; stable and v6 are unchanged')
    if not 1<=args.width<=512 or not 1<=args.height<=512 or not 1<=len(args.mask)<=16:p.error('research dimensions/object budget')
    if args.output.exists():p.error('output exists; no clobber')
    rgba=np.frombuffer(exact(args.rgba,args.width*args.height*4),np.uint8).reshape(args.height,args.width,4)
    masks=[np.frombuffer(exact(m,args.width*args.height),np.uint8).reshape(args.height,args.width).copy() for m in args.mask]
    try:
        result,feedback=plan(rgba,masks,allow_soft_alpha=args.diagnostic_soft_alpha)
    except (ValueError,RuntimeError) as error:
        print(f'BLOCKED: {error}',flush=True);raise SystemExit(3)
    write_plan(result,args.output);feedback.tofile(args.output/'feedback.rgba')
    print(json.dumps(dict(events=len(result['strokes']),samples=sum(len(s['geometry']) for s in result['strokes']))))
if __name__=='__main__':main()
