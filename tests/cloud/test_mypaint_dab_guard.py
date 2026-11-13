"""Exact callback boundary checks with ctypes ABI stub; no optional runtime needed."""
import ctypes as C
import unittest
from types import SimpleNamespace
from cloud.dab_guard import DabGuard
class Surface(C.Structure):_fields_=[('draw_dab',C.c_void_p)]
CALL=C.CFUNCTYPE(C.c_int,C.c_void_p,*([C.c_float]*13))
class ExactDabGuard(unittest.TestCase):
 def invoke(self,x=7.,y=4.,radius=1.,origin=(10,10),full=(100,100),aspect=1.):
  calls=[]
  @CALL
  def original(surface,*args):calls.append(args);return 1
  surface=Surface(C.cast(original,C.c_void_p).value);a=SimpleNamespace(C=C,I=C.c_int,F=C.c_float,P=C.c_void_p,Surface=Surface)
  guard=DabGuard(a,C.addressof(surface),10,10,{'full_canvas':list(full),'origin':list(origin)})
  ptr=surface.draw_dab;callback=CALL(ptr)
  result=callback(C.addressof(surface),x,y,radius,0,0,0,1,1,1,aspect,0,0,0)
  error=None
  try:guard.check()
  except ValueError as e:error=str(e)
  guard.restore();self.assertEqual(surface.draw_dab,C.cast(original,C.c_void_p).value)
  return result,calls,error
 def test_internal_exact_endpoint_rejected(self):
  for x,y in [(8,4),(4,8)]:
   result,calls,error=self.invoke(x,y);self.assertEqual(result,0);self.assertEqual(calls,[]);self.assertIn('internal ROI',error)
 def test_subpixel_float32_roundup(self):
  # Python double would be below10; surface float32 sum rounds up to10.
  result,calls,error=self.invoke(x=7.9999999);self.assertEqual(result,0);self.assertEqual(calls,[]);self.assertIsNotNone(error)
  result,calls,error=self.invoke(x=7.99999);self.assertEqual(result,1);self.assertEqual(len(calls),1);self.assertIsNone(error)
 def test_negative_fringe_rejected(self):
  result,calls,error=self.invoke(x=1.99999);self.assertEqual(result,0);self.assertEqual(calls,[])
 def test_actual_canvas_edge_may_clip(self):
  result,calls,error=self.invoke(x=9,y=9,origin=(0,0),full=(10,10));self.assertEqual(result,1);self.assertIsNone(error)
  result,calls,error=self.invoke(x=0,y=0,origin=(0,0),full=(10,10));self.assertEqual(result,1);self.assertIsNone(error)
 def test_no_ellipse_and_nonfinite(self):
  for kwargs in [dict(aspect=3),dict(x=float('nan')),dict(radius=float('inf'))]:
   result,calls,error=self.invoke(**kwargs);self.assertEqual(result,0);self.assertEqual(calls,[]);self.assertIsNotNone(error)
if __name__=='__main__':unittest.main()
