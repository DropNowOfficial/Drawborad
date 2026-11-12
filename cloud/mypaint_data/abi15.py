"""Isolated ctypes experiment using public v1.5.1 declarations saved in headers/.
Only preinstalled runtime is executed; no library installation or production edits.
"""
import ctypes as C, math, json, hashlib
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parent
libpath='/lib/x86_64-linux-gnu/libmypaint-1.5.so.1'
lib=C.CDLL(libpath)
P=C.c_void_p;I=C.c_int;F=C.c_float
class Rect(C.Structure):_fields_=[('x',I),('y',I),('width',I),('height',I)]
class Surface(C.Structure):_fields_=[(n,P) for n in ['draw_dab','get_color','begin_atomic','end_atomic','destroy','save_png']]+[('refcount',I)]
class Tiled(C.Structure):_fields_=[('parent',Surface),('start',P),('end',P),('symmetry',I),('center',F),('queue',P),('dirty',Rect),('threadsafe',I),('tile_size',I)]
class Request(C.Structure):_fields_=[('tx',I),('ty',I),('readonly',I),('buffer',C.POINTER(C.c_uint16)),('context',P),('thread_id',I),('mipmap_level',I)]
def bind(name,args,ret=None):
 f=getattr(lib,name);f.argtypes=args;f.restype=ret;return f
new=bind('mypaint_fixed_tiled_surface_new',[I,I],P)
interface=bind('mypaint_fixed_tiled_surface_interface',[P],P)
reqinit=bind('mypaint_tile_request_init',[C.POINTER(Request),I,I,I,I])
start=bind('mypaint_tiled_surface_tile_request_start',[P,C.POINTER(Request)])
end=bind('mypaint_tiled_surface_tile_request_end',[P,C.POINTER(Request)])
begin=bind('mypaint_surface_begin_atomic',[P]);finish=bind('mypaint_surface_end_atomic',[P,C.POINTER(Rect)])
bnew=bind('mypaint_brush_new',[],P);load=bind('mypaint_brush_from_string',[P,C.c_char_p],I)
stroke=bind('mypaint_brush_stroke_to',[P,P,F,F,F,F,F,C.c_double],I)
reset=bind('mypaint_brush_new_stroke',[P]);clear=bind('mypaint_brush_reset',[P]);bfree=bind('mypaint_brush_unref',[P]);sfree=bind('mypaint_surface_unref',[P])
setting=bind('mypaint_brush_setting_from_cname',[C.c_char_p],I);setbase=bind('mypaint_brush_set_base_value',[P,I,F])
