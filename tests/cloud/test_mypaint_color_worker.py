"""Optional real pinned MyPaint color tests; no historical worker fixture required."""
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud import mypaint_worker as m
try:
 from PIL import Image
except ImportError:
 Image=None

class MyPaintColorTest(unittest.TestCase):
 def setUp(self):
  self.body={'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'0','points':[{'x':100,'y':100,'pressure':.35,'time_ms':0},{'x':135,'y':112,'pressure':.6,'time_ms':40}]}
  self.state={'width':256,'height':256,'revision':'0','document_id':'00000000-0000-4000-8000-000000000001'}

 def test_color_validation_without_optional_runtime(self):
  for valid in ['#ffffff','#FF0000','#000000']:
   self.assertEqual(m.validate_request(dict(self.body,color=valid),self.state),self.body['points'])
  for invalid in [None,True,1,'red','#fff','#12345678','#GG0000',' #123456']:
   with self.subTest(color=invalid),self.assertRaises(ValueError):
    m.validate_request(dict(self.body,color=invalid),self.state)
  for field in ['width','layer_id']:
   with self.subTest(field=field),self.assertRaises(ValueError):
    m.validate_request(dict(self.body,**{field:1}),self.state)

 def exercise_real(self,crop):
  if Image is None:self.skipTest('optional Pillow PNG decoder is unavailable')
  ok,reason=m.available(256,256)
  if not ok:self.skipTest('optional pinned MyPaint runtime unavailable: '+reason)
  state=dict(self.state,capabilities=['append_brush_crop'] if crop else [])
  with tempfile.TemporaryDirectory(prefix='mypaint-color-') as tmp:
   def run(label,body,optimize='0'):
    folder=Path(tmp)/label;folder.mkdir()
    with patch.dict(os.environ,{'PYTHONOPTIMIZE':optimize}):
     png,manifest=m.render(body,state,folder,threading.Event())
    with Image.open(png) as image:pixels=image.convert('RGBA').tobytes()
    return pixels,png.read_bytes(),manifest.read_bytes(),json.loads(manifest.read_text())
   black=run('black',self.body)
   explicit=run('explicit-black',dict(self.body,color='#000000'))
   self.assertEqual(black[:3],explicit[:3],'default black must retain exact same-version file identity')
   red=run('red',dict(self.body,color='#ff0000'))
   optimized=run('optimized',dict(self.body,color='#FF0000'),'1')
   self.assertEqual(red[:3],optimized[:3])
   self.assertEqual(red[0][3::4],black[0][3::4])
   self.assertTrue(any(black[0][3::4]),'must render real nonempty ink')
   self.assertEqual(set(red[0][0::4]),{255})
   self.assertEqual(set(red[0][1::4]),{0})
   self.assertEqual(set(red[0][2::4]),{0})
   metadata=red[3]['worker_manifest'] if crop else red[3]
   original=black[3]['worker_manifest'] if crop else black[3]
   self.assertEqual(metadata['color'],'#FF0000')
   self.assertIn('recolor_policy',metadata)
   self.assertNotIn('recolor_policy',original)
   self.assertEqual(metadata['white_surface_rgba_sha256'],original['white_surface_rgba_sha256'])
   if crop:
    self.assertEqual(set(red[3]['helper_sha256']),{'crop_plan.py','dab_guard.py'})

 def test_real_legacy_color_and_optimized(self):self.exercise_real(False)
 def test_real_crop_color_and_optimized(self):self.exercise_real(True)

 def test_real_empty_ink_rejected_without_result(self):
  ok,reason=m.available(256,256)
  if not ok:self.skipTest('optional pinned MyPaint runtime unavailable: '+reason)
  cases=[
   [{'x':100,'y':100,'pressure':.65,'time_ms':0}],
   [{'x':100,'y':100,'pressure':.65,'time_ms':0},{'x':102,'y':100,'pressure':.65,'time_ms':30}],
   [{'x':50,'y':100,'pressure':0,'time_ms':0},{'x':150,'y':100,'pressure':0,'time_ms':100}],
  ]
  for crop in [False,True]:
   for points in cases:
    with self.subTest(crop=crop,points=points),tempfile.TemporaryDirectory(prefix='mypaint-empty-') as tmp:
     state=dict(self.state,capabilities=['append_brush_crop'] if crop else [])
     with self.assertRaisesRegex(RuntimeError,'no visible ink'):
      m.render(dict(self.body,points=points),state,Path(tmp),threading.Event())
     self.assertFalse(list(Path(tmp).rglob('*.png')))
     self.assertFalse(list(Path(tmp).rglob('manifest.json')))
     self.assertFalse(list(Path(tmp).rglob('crop-manifest.json')))

if __name__=='__main__':unittest.main()
