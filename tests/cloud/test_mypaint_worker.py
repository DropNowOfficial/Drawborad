import os,sys,tempfile,threading,unittest,json,hashlib
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from cloud import mypaint_worker as m
from PIL import Image
class WorkerTest(unittest.TestCase):
 def setUp(self):
  self.state={'width':513,'height':257,'revision':'4'}
  self.body={'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'4','points':[{'x':100+i*3,'y':120,'pressure':.2+.3*(i/100),'time_ms':i*5} for i in range(101)]}
 def test_optional_platform_import(self):
  with patch.object(m,'resource',None):self.assertFalse(m.available(128,128)[0])
  self.assertEqual(m.dimensions('513','257'),(513,257))
  for bad in ['1.0','1e3','-1',' 5',True]:
   with self.assertRaises(ValueError):m.dimensions(bad,128)
 def test_reject_fields_bounds_and_budget(self):
  for key,value in [('width',5),('layer_id','f'*32)]:
   with self.assertRaises(ValueError):m.validate_request(dict(self.body,**{key:value}),self.state)
  self.assertEqual(m.validate_request(dict(self.body,color='#ffffff'),self.state),self.body['points'])
  for color in ['#fff','#GG0000',None,True]:
   with self.assertRaises(ValueError):m.validate_request(dict(self.body,color=color),self.state)
  bad=dict(self.body,points=[{'x':float('nan'),'y':0,'pressure':1,'time_ms':0}])
  with self.assertRaises(ValueError):m.validate_request(bad,self.state)
  self.assertFalse(m.available(2048,2048)[0])
 def test_real_worker_transparency_replay_tag_and_white_equivalence(self):
  if not m.available(513,257)[0]:self.skipTest('optional pinned system library unavailable; real worker requires it')
  with tempfile.TemporaryDirectory() as root:
   paths=[]
   for index in range(2):
    folder=Path(root)/str(index);folder.mkdir()
    revision=str(4+index)
    paths.append(m.render(dict(self.body,expected_revision=revision),dict(self.state,revision=revision),folder,threading.Event()))
   png,manifest=paths[0];doc=json.loads(manifest.read_text());im=Image.open(png).convert('RGBA')
   self.assertEqual(im.size,(513,257));self.assertEqual(im.getpixel((0,0)),(0,0,0,0));self.assertEqual(im.getpixel((512,256)),(0,0,0,0))
   self.assertTrue(any(p[3]>0 for p in im.getdata()))
   self.assertEqual(hashlib.sha256(im.tobytes()).hexdigest(),doc['rgba_sha256'])
   white=Image.alpha_composite(Image.new('RGBA',im.size,'white'),im)
   self.assertEqual(hashlib.sha256(white.tobytes()).hexdigest(),doc['white_surface_rgba_sha256'])
   with Image.open(png) as tagged:self.assertEqual(tagged.info['DrawboradReplaySHA256'],m.sha(manifest))
   self.assertEqual(im.tobytes(),Image.open(paths[1][0]).convert('RGBA').tobytes())
   self.assertEqual(png.read_bytes(),paths[1][0].read_bytes())
   self.assertNotIn('expected_revision',doc)
   self.assertEqual(manifest.read_bytes(),paths[1][1].read_bytes(),'same render inputs at different revisions share persistent identity')
   flag=threading.Event();flag.set()
   with self.assertRaises(RuntimeError):m.render(self.body,self.state,Path(root),flag)
   cancel=threading.Event();timer=threading.Timer(.05,cancel.set);timer.start();folder=Path(root)/'cancel';folder.mkdir()
   try:
    with self.assertRaises(RuntimeError):m.render(self.body,self.state,folder,cancel)
   finally:timer.join()
   self.assertFalse((folder/'stroke.png').exists())
 def test_python_optimization_does_not_skip_preset(self):
  if not m.available(513,257)[0]:self.skipTest('optional pinned runtime unavailable')
  with tempfile.TemporaryDirectory() as root:
   pixels=[]
   for optimize in ['0','1']:
    folder=Path(root)/optimize;folder.mkdir()
    with patch.dict(os.environ,{'PYTHONOPTIMIZE':optimize}):
     png,manifest=m.render(self.body,self.state,folder,threading.Event())
    pixels.append(Image.open(png).convert('RGBA').tobytes())
   self.assertEqual(pixels[0],pixels[1]);self.assertTrue(any(pixels[0][3::4]),'preset must actually produce ink')
if __name__=='__main__':unittest.main()
