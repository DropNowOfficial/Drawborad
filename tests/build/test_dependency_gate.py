"""Mutant contract: dropping a hash/license/path/qualification check fails here."""
import hashlib, importlib.util, json, pathlib, subprocess, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('gate',ROOT/'tools/dependency_gate.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
class DependencyGate(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=pathlib.Path(self.tmp.name)
  (self.root/'lib.dll').write_bytes(b'fixture dll bytes, not a real binary')
  self.entry={'name':'fixture','version':'1.0.0','path':'lib.dll','sha256':hashlib.sha256((self.root/'lib.dll').read_bytes()).hexdigest(),'size':(self.root/'lib.dll').stat().st_size,'license':'MIT','abi':'fixture-only','qualification':{'status':'qualified','evidence':'tests/fixture'},'publisher_url':'https://example.invalid/lib.dll','security_review':'fixture only'}
  self.lock={'schema':1,'profiles':{'fixture':{'required':['fixture'],'artifacts':[self.entry]}}}
 def code(self):return g.verify(self.lock,self.root,'fixture')
 def test_valid_local_manifest(self):self.assertEqual(self.code(),[])
 def test_missing_digest(self):self.entry.pop('sha256');self.assertIn('MissingDigest',self.code())
 def test_changed_archive(self):(self.root/'lib.dll').write_bytes(b'tampered!');self.assertIn('DigestMismatch',self.code())
 def test_missing_dll(self):(self.root/'lib.dll').unlink();self.assertIn('MissingArtifact',self.code())
 def test_missing_license(self):self.entry['license']='';self.assertIn('LicenseBlocked',self.code())
 def test_unqualified(self):self.entry['qualification']['status']='candidate';self.assertIn('Unqualified',self.code())
 def test_path_escape(self):self.entry['path']='../outside.dll';self.assertIn('PathEscape',self.code())
 def test_absolute_path(self):self.entry['path']=str(self.root/'lib.dll');self.assertIn('PathEscape',self.code())
 def test_duplicate_role(self):self.lock['profiles']['fixture']['artifacts'].append(self.entry.copy());self.assertIn('DuplicateArtifact',self.code())
 def test_required_role(self):self.lock['profiles']['fixture']['required'].append('qt');self.assertIn('MissingArtifact',self.code())
 def test_verify_without_network(self):
  from unittest.mock import patch
  with patch('socket.socket',side_effect=AssertionError('NETWORK_ATTEMPT')):self.assertEqual(self.code(),[])
 def test_gate_fails_closed_for_unqualified_windows(self):
  lock=json.loads((ROOT/'dependencies/lock.json').read_text());self.assertIn('Unqualified',g.verify(lock,self.root,'windows-x64'))
if __name__=='__main__':unittest.main()
