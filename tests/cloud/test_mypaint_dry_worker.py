"""Optional pinned-runtime dry-media checks; no private artwork fixtures."""
import json
from pathlib import Path
import sys,tempfile,threading,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud import mypaint_worker as m

class DryPresetTest(unittest.TestCase):
 def test_whitelist_and_legacy_refusal(self):
  self.assertEqual(m.preset_name(),'pointy_ink')
  self.assertEqual(m.preset_name('dry_brush'),'dry_brush')
  for bad in [None,True,1,{},'../dry_brush','charcoal','DRY_BRUSH']:
   with self.subTest(value=bad),self.assertRaises(ValueError):m.preset_name(bad)
  with tempfile.TemporaryDirectory() as d,self.assertRaisesRegex(ValueError,'cropped native support'):
   m.render_legacy({'preset':'dry_brush'},{},Path(d),threading.Event())

 def test_real_dry_repeat_and_metadata(self):
  ok,reason=m.available(512,512)
  if not ok:self.skipTest('optional pinned MyPaint runtime unavailable: '+reason)
  try:m.selected_pins('dry_brush')
  except (ValueError,OSError) as e:self.skipTest('optional pinned dry preset unavailable: '+str(e))
  from PIL import Image
  state={'width':2048,'height':2048,'revision':'1','document_id':'0'*32,'capabilities':['append_brush_crop']}
  body={'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'1','preset':'dry_brush','color':'#234523','points':[{'x':800+i*3,'y':850+i*.2,'pressure':.55,'time_ms':i*10} for i in range(41)]}
  with tempfile.TemporaryDirectory() as tmp:
   outputs=[]
   for name in ['first','repeat']:
    folder=Path(tmp)/name;folder.mkdir();png,manifest=m.render(body,state,folder,threading.Event())
    with Image.open(png) as im:rgba=im.convert('RGBA').tobytes()
    meta=json.loads(manifest.read_text());worker=meta['worker_manifest']
    self.assertTrue(any(rgba[3::4]));self.assertEqual(worker['preset'],'dry_brush')
    self.assertEqual(worker['pins']['preset'],m.DRY_SHA)
    self.assertEqual(worker['backend'],'mypaint-dry-brush-raster-v1')
    self.assertTrue(worker['actual_dab_guard']['passed'])
    self.assertIn('runtime_guarded_proposal',meta);self.assertNotIn('conservative_bound',meta)
    self.assertEqual(meta['full_canvas'],[2048,2048]);self.assertFalse(meta['canvas_resized'])
    outputs.append((rgba,png.read_bytes(),manifest.read_bytes()))
   self.assertEqual(outputs[0],outputs[1],'same-machine repeat only; not a cross-platform guarantee')
   folder=Path(tmp)/'empty';folder.mkdir()
   body['points']=[dict(p,pressure=0) for p in body['points']]
   with self.assertRaisesRegex(RuntimeError,'no visible ink'):m.render(body,state,folder,threading.Event())
   self.assertFalse(list(folder.rglob('*.png')))

if __name__=='__main__':unittest.main()
