"""Conservative ROI planner for the fixed pinned pointy-ink recipe.
Full canvas dimensions are preserved; oversized strokes are rejected.
"""
import hashlib,json,math,sys,threading
from pathlib import Path
import mypaint_worker as worker
PRESET_SHA='420f8d0d0134f77061a6c8e04cb01332eaa1458fd9d86129acc16bcea8800064'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def bound():
 # Only the reviewed immutable pointy preset is supported; not arbitrary mappings.
 expected=PRESET_SHA
 if sha(worker.DATA/'pointy_ink.myb')!=expected:raise ValueError('Preset hash mismatch')
 settings=json.loads((worker.DATA/'pointy_ink.myb').read_text())['settings']
 ok,reason=worker.available(128,128)
 if not ok:raise ValueError(reason)
 import importlib.util
 spec=importlib.util.spec_from_file_location('crop_abi',worker.DATA/'abi15.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
 brush=a.bnew()
 if not brush or a.load(brush,(worker.DATA/'pointy_ink.myb').read_bytes())!=1:raise RuntimeError('MyPaint preset load failed')
 get=a.bind('mypaint_brush_get_base_value',[a.P,a.I],a.F);used=a.bind('mypaint_brush_get_inputs_used_n',[a.P,a.I],a.I)
 try:
  for name in ['offset_x','offset_y','offset_angle','offset_angle_asc','offset_angle_view','offset_angle_2','offset_angle_2_asc','offset_angle_2_view']:
   setting=a.setting(name.encode())
   if setting>=0 and (get(brush,setting)!=0 or used(brush,setting)!=0):raise ValueError('Runtime default introduces unsupported '+name)
 finally:a.bfree(brush)

 for key in ['offset_by_random','offset_by_speed','radius_by_random','tracking_noise','slow_tracking','slow_tracking_per_dab','pressure_gain_log','snap_to_pixel','smudge']:
  if settings[key]!={'base_value':0.0,'inputs':{}}:raise ValueError('Unsupported dynamic '+key)
 if settings['elliptical_dab_ratio']!={'base_value':1.0,'inputs':{}}:raise ValueError('Unsupported elliptical dab')
 radius=settings['radius_logarithmic'];mapping=radius['inputs']
 if set(mapping)!={'pressure','speed1','stroke'}:raise ValueError('Unsupported radius inputs')
 if mapping['pressure']!=[[0.,0.],[1.,3.36]] or mapping['speed1']!=[[0.,.0991666666666667],[1.,-.14]]:raise ValueError('Unreviewed mapping')
 # Mapping extrapolates! SPEED1 can be negative, so max(control point y) alone
 # underestimates radius. From official brush.c speed_mapping constraints:
 gamma=math.exp(settings['speed1_gamma']['base_value']);m=.015*(45+gamma);q=.5-m*math.log(45+gamma)
 min_speed1=math.log(gamma)*m+q # norm_speed is nonnegative; speed mapping monotone.
 speed_points=mapping['speed1'];speed_max=speed_points[0][1]+min_speed1*(speed_points[1][1]-speed_points[0][1])
 stroke_max=max(y for x,y in mapping['stroke']) # engine explicitly min(STROKE,1), starting at0.
 max_log=radius['base_value']+3.36+speed_max+stroke_max
 max_radius=min(1000.,math.exp(max_log))
 # Anti-alias radius_new = optical_radius + min_fadeout/2 <= radius + .5;
 # tiled surface expands by1px; add2px explicit floating/rounding guard.
 aa=settings['anti_aliasing']
 if aa!={'base_value':1.0,'inputs':{}}:raise ValueError('Unsupported AA preset')
 margin=math.ceil(max_radius+.5+1+2)
 return {'margin':margin,'maximum_radius_before_aa':max_radius,'speed1_minimum':min_speed1,'max_radius_log':max_log,'bound_scope':'reviewed pointy preset, pressure0..1, no offsets/noise/smudge, viewzoom1'}
def plan(canvas,points):
 w,h=canvas
 if type(w)!=int or type(h)!=int or min(w,h)<1 or max(w,h)>8192 or w*h>20000000:raise ValueError('Global canvas outside prototype limit')
 worker.validate_request({'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'1','points':points},{'width':w,'height':h,'revision':'1'})
 proof=bound();margin=proof['margin']
 x0=max(0,math.floor(min(p['x'] for p in points))-margin);y0=max(0,math.floor(min(p['y'] for p in points))-margin)
 x1=min(w,math.ceil(max(p['x'] for p in points))+margin+1);y1=min(h,math.ceil(max(p['y'] for p in points))+margin+1)
 if max(x1-x0,y1-y0)>2048 or (x1-x0)*(y1-y0)>1048576:raise ValueError('Conservative stroke ROI exceeds budget; refusing, not clipping ink')
 local=[dict(p,x=p['x']-x0,y=p['y']-y0) for p in points]
 return [x0,y0,x1-x0,y1-y0],local,proof
