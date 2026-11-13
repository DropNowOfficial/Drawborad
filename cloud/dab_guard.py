"""Legacy MyPaintSurfaceDrawDabFunction: int (Surface*, 13 floats).
Exact declaration: official libmypaint v1.5.1/v1.6.1 mypaint-surface.h.
Fail-closed actual-dab guard; never raises through the ctypes callback. Any failure poisons the entire stroke.
"""
import math
class DabGuard:
 def __init__(self,a,iface,w,h,geometry):
  self.a=a;self.surface=a.C.cast(iface,a.C.POINTER(a.Surface));self.original=self.surface.contents.draw_dab
  fw,fh=geometry['full_canvas'];ox,oy=geometry['origin']
  if any(type(v)!=int for v in (fw,fh,ox,oy,w,h)) or min(fw,fh,w,h)<=0 or min(ox,oy)<0 or ox+w>fw or oy+h>fh:raise ValueError('Invalid actual dab guard geometry')
  self.count=0;self.error=None
  signature=a.C.CFUNCTYPE(a.I,a.P,*([a.F]*13));self.delegate=signature(self.original)
  def callback(surface,*args):
   try:
    if self.error:return 0
    self.count+=1
    if not all(math.isfinite(v) for v in args):raise ValueError('Nonfinite actual dab')
    x,y,r=args[:3];aspect=args[9]
    if r<0 or aspect!=1:raise ValueError('Unsupported actual dab geometry')
    f32=lambda value:a.F(value).value
    extent=f32(r+1.0) # surface float32 fringe and inclusive integer bbox
    left=math.floor(f32(x-extent));right=math.floor(f32(x+extent));top=math.floor(f32(y-extent));bottom=math.floor(f32(y+extent))
    if ((ox>0 and left<0) or (oy>0 and top<0) or (ox+w<fw and right>=w) or (oy+h<fh and bottom>=h)):
     raise ValueError('Actual dab exceeds internal ROI edge; entire stroke rejected')
    return self.delegate(surface,*args)
   except BaseException as exc:
    self.error=str(exc) or 'Actual dab callback failed'
    return 0
  self.callback=signature(callback) # strong reference through native rendering
  self.surface.contents.draw_dab=a.C.cast(self.callback,a.P).value
 def check(self):
  if self.error:raise ValueError(self.error)
 def restore(self):
  self.surface.contents.draw_dab=self.original
