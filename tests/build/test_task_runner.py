"""Only tests RED/GREEN process-result classification, not product behavior."""
import contextlib, io, pathlib, runpy, subprocess, sys, unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[2]
class TaskRunnerResults(unittest.TestCase):
 def execute(self, phase, code, output):
  def run(argv, **kwargs):
   return subprocess.CompletedProcess(argv,code if argv[0]=='ctest' else 0,output if argv[0]=='ctest' else '')
  with patch.object(sys,'argv',['test_task.py','--id','F06','--phase',phase]),patch('subprocess.run',side_effect=run),contextlib.redirect_stdout(io.StringIO()):
   try:runpy.run_path(str(ROOT/'tools/test_task.py'),run_name='__main__')
   except SystemExit as e:return e.code
  self.fail('runner did not exit')
 def test_green_skipped_task_is_blocked(self):self.assertEqual(self.execute('Green',0,'1/1 F06 ***Skipped\n100% tests passed'),2)
 def test_red_crash_is_not_behavior_red(self):self.assertEqual(self.execute('Red',8,'ASSERT_FAIL x\n***Exception: SegFault'),2)
 def test_red_assertion_is_red(self):self.assertEqual(self.execute('Red',8,'ASSERT_FAIL expected new behavior'),0)
 def test_green_task_and_suite_pass(self):self.assertEqual(self.execute('Green',0,'1/1 F06 Passed\n100% tests passed'),0)
if __name__=='__main__':unittest.main()
