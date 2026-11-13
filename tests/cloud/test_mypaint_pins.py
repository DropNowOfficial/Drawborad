import importlib.util,json,os,sys,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from cloud import mypaint_worker as m
spec=importlib.util.spec_from_file_location('provision',ROOT/'tools/experimental/mypaint/provision_ci_pins.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class PinsTest(unittest.TestCase):
 def test_operator_override_only_and_strict_schema(self):
  with patch.dict(os.environ,{},clear=True):default=m.load_pins()
  with tempfile.TemporaryDirectory() as td:
   f=Path(td)/'pins.json';f.write_text(json.dumps(default))
   with patch.dict(os.environ,{'DRAWBORAD_MYPAINT_PINS_FILE':str(f)}):self.assertEqual(m.load_pins(),default)
   with patch.dict(os.environ,{'DRAWBORAD_MYPAINT_PINS_FILE':'relative.json'}):
    with self.assertRaises(ValueError):m.load_pins()
   f.write_text(json.dumps(dict(default,operator='extra')))
   with patch.dict(os.environ,{'DRAWBORAD_MYPAINT_PINS_FILE':str(f)}):self.assertFalse(m.available(128,128)[0])
   f.write_text(json.dumps(dict(default,libmypaint='0'*64)))
   with patch.dict(os.environ,{'DRAWBORAD_MYPAINT_PINS_FILE':str(f)}):self.assertFalse(m.available(128,128)[0])
  body={'type':'draw_stroke','brush':'mypaint_ink','expected_revision':'1','points':[],'pins_file':'/tmp/arbitrary'}
  with self.assertRaises(ValueError):m.validate_request(body,{'revision':'1','width':128,'height':128})
 def test_provision_guards_and_no_clobber(self):
  with tempfile.TemporaryDirectory() as td:
   args=SimpleNamespace(allow_ci_provision=True,pins_file=str(Path(td)/'pins.json'),receipt_file=str(Path(td)/'receipt.json'),expected_libmypaint_version='1',expected_json_c_version='2')
   with patch.dict(os.environ,{'GITHUB_ACTIONS':'false'}):
    with self.assertRaises(ValueError):p.provision(args)
   with patch.dict(os.environ,{'GITHUB_ACTIONS':'true'}):
    args.allow_ci_provision=False
    with self.assertRaises(ValueError):p.provision(args)
    args.allow_ci_provision=True
    def info(name,path,expected):return {'package':name,'version':expected,'sha256':'a'*64}
    with patch.object(p,'package_info',side_effect=info):p.provision(args)
    first=Path(args.pins_file).read_bytes()
    self.assertEqual(set(json.loads(first)),{'libmypaint','json_c','preset','abi'})
    with self.assertRaises(FileExistsError):p.provision(args)
    self.assertEqual(Path(args.pins_file).read_bytes(),first)
    args.pins_file=str(p.DATA/'pins.json')
    with self.assertRaises(ValueError):p.provision(args)
 def test_package_version_and_ownership_validation(self):
  with tempfile.TemporaryDirectory() as td:
   f=Path(td)/'library.so';f.write_bytes(b'fixture-not-executed')
   good=SimpleNamespace(stdout='install ok installed\t1.2-3\tamd64')
   owned=SimpleNamespace(stdout=str(f)+'\n')
   with patch.object(p.subprocess,'run',side_effect=[good,owned]):self.assertEqual(p.package_info('example',f,'1.2-3')['version'],'1.2-3')
   with patch.object(p.subprocess,'run',return_value=good):
    with self.assertRaises(ValueError):p.package_info('example',f,'different')
   with patch.object(p.subprocess,'run',side_effect=[good,SimpleNamespace(stdout='/other/file')]):
    with self.assertRaises(ValueError):p.package_info('example',f,'1.2-3')
if __name__=='__main__':unittest.main()
