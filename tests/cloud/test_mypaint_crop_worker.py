import sys,tempfile,threading,unittest,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud import mypaint_worker as w
class Tests(unittest.TestCase):
 def setUp(self):
  if not w.available(128,128)[0]:self.skipTest("optional pinned runtime unavailable; real execution required separately")
 def body(self):return {'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'1','points':[{'x':500,'y':700,'pressure':.3,'time_ms':0},{'x':600,'y':710,'pressure':.4,'time_ms':200}]}
 def state(self):return {'width':'3300','height':'5100','revision':'1','document_id':'a'*32,'capabilities':['append_brush_crop']}
 def test_large_canvas_real_child(self):
  with tempfile.TemporaryDirectory() as d:
   png,manifest=w.render(self.body(),self.state(),d,threading.Event());m=json.loads(manifest.read_text())
   self.assertEqual(m['full_canvas'],[3300,5100]);self.assertFalse(m['canvas_resized']);self.assertLess(m['roi_size'][0]*m['roi_size'][1],1048576);self.assertTrue(m['worker_manifest']['actual_dab_guard']['passed'])
 def test_reject_and_cancel(self):
  self.assertFalse(w.available_crop('8193','100')[0]);self.assertFalse(w.available_crop('8192','8192')[0])
  with tempfile.TemporaryDirectory() as d:
   event=threading.Event();event.set()
   with self.assertRaises(RuntimeError):w.render(self.body(),self.state(),d,event)
   self.assertFalse((Path(d)/'stroke.png').exists())
  with tempfile.TemporaryDirectory() as d:
   body=self.body();body['points'][1].update(x=3200,y=5000)
   with self.assertRaisesRegex(RuntimeError,'ROI exceeds budget'):w.render(body,self.state(),d,threading.Event())
   self.assertFalse((Path(d)/'stroke.png').exists());self.assertFalse((Path(d)/'crop-manifest.json').exists())
 def test_legacy(self):
  state=dict(self.state(),width='128',height='128',capabilities=['append_brush_layer']);body=self.body()
  for p in body['points']:p.update(x=p['x']/10,y=p['y']/10)
  with tempfile.TemporaryDirectory() as d:
   png,m=w.render(body,state,d,threading.Event());self.assertEqual(json.loads(m.read_text())['canvas'],[128,128])
if __name__=='__main__':unittest.main()
