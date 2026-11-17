"""Real native CLI opt-in; no model or service substitutes."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

engine = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as root:
    out = Path(root) / "painting"
    run = subprocess.run([engine, "smoke", "--output", str(out), "--planner", "multiscale"], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    report = json.loads(run.stdout)
    assert report["planner"] == "multiscale"
    assert report["reopenReplayMatches"] is True
    assert report["replaySourceAssetReads"] == 0
    assert (out / "project.draw").stat().st_size > 0
    invalid = subprocess.run([engine, "smoke", "--output", str(Path(root) / "invalid"), "--planner", "unknown"], capture_output=True)
    assert invalid.returncode == 2
    assert not (Path(root) / "invalid").exists()
