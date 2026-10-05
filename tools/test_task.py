"""Lab RED/GREEN runner. exit 2 means environment/compilation BLOCKED, never RED."""
import argparse,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--id',required=True);p.add_argument('--phase',choices=['Red','Green'],required=True);p.add_argument('--preset',default='lab-debug');a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
def run(argv):
 try:return subprocess.run(argv,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 except OSError as e:print('BLOCKED:',e);sys.exit(2)
for cmd in [['cmake','--preset',a.preset],['cmake','--build','--preset',a.preset]]:
 r=run(cmd)
 if r.returncode:print(r.stdout);sys.exit(2)
r=run(['ctest','--preset',a.preset,'-R','^'+a.id+'$','--no-tests=error','--output-on-failure']);print(r.stdout)
blocked_markers=('***skipped','***exception','segmentation fault','segfault','no tests were found','***timeout')
if any(marker in r.stdout.lower() for marker in blocked_markers):print('BLOCKED: task did not finish its behavior assertions');sys.exit(2)
if a.phase=='Red':
 real_failure= r.returncode!=0 and ('ASSERT_FAIL' in r.stdout or 'AssertionError:' in r.stdout)
 if not real_failure:print('RED not demonstrated');sys.exit(2)
 print('RED_VERIFIED');sys.exit(0)
if r.returncode:sys.exit(1)
r=run(['ctest','--preset',a.preset,'--no-tests=error','--output-on-failure']);print(r.stdout);sys.exit(0 if r.returncode==0 else 1)
