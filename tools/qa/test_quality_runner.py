import json
import subprocess
import sys

proc = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=False)
assert proc.returncode == 77, (proc.returncode, proc.stdout, proc.stderr)
data = json.loads(proc.stdout)
assert data == {
    "fidelity": "Blocked",
    "layer_integrity": "Blocked",
    "natural_process": "Blocked",
    "accepted": False,
}
print("PASS: unverified E06 runner reports three independent blocked gates")
