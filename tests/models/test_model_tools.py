import json
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
qualify = root / "tools" / "models" / "qualify.py"
export = root / "tools" / "models" / "export_sam21.py"

import importlib.util
spec = importlib.util.spec_from_file_location("drawborad_export_sam21", export)
export_module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(export_module)
runtime = export_module.runtime_package_manifest(
    variant="tiny", source_commit="a"*40, checkpoint_sha256="b"*64,
    encoder_file="encoder.onnx", encoder_sha256="c"*64,
    decoder_file="decoder.onnx", decoder_sha256="d"*64)
assert runtime["schema"] == 1
assert runtime["taskId"] == "sam21.image.v1"
assert runtime["weights"] == "encoder.onnx"
assert runtime["decoderWeights"] == "decoder.onnx"
assert runtime["provider"] == "cpu"
assert runtime["abi"] == "sam21.image.v1"
assert runtime["license"] == "Apache-2.0"

def run(*args):
    return subprocess.run([sys.executable, *map(str, args)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)

assert run(export, "--help").returncode == 0, "export_sam21.py --help must work without loading torch"

with tempfile.TemporaryDirectory() as td:
    td = pathlib.Path(td)
    recipe = td / "recipe.json"
    fixtures = td / "fixtures.json"
    report = td / "report.json"
    recipe.write_text(json.dumps({
        "taskId": "sam21.image.v1",
        "variant": "tiny",
        "minMaskIou": 0.995,
        "maxPeakGpuBytes": 6 * 1024**3,
    }), encoding="utf-8")
    fixtures.write_text(json.dumps({
        "sourceCommit": "a" * 40,
        "checkpointSha256": "b" * 64,
        "networkAttempts": 0,
        "cases": [
            {"case": i, "maskIou": 0.999, "cpuShape": [1, 3, 256, 256],
             "cudaShape": [1, 3, 256, 256]}
            for i in range(20)
        ],
        "peakGpuBytes": 2 * 1024**3,
    }), encoding="utf-8")
    ok = run(qualify, "--recipe", recipe, "--fixtures", fixtures, "--report", report)
    assert ok.returncode == 0, ok.stderr
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["pass"] is True
    assert data["caseCount"] == 20
    assert data["networkAttempts"] == 0

    bad = json.loads(fixtures.read_text(encoding="utf-8"))
    bad["cases"][7]["maskIou"] = 0.90
    fixtures.write_text(json.dumps(bad), encoding="utf-8")
    failed = run(qualify, "--recipe", recipe, "--fixtures", fixtures, "--report", report)
    assert failed.returncode != 0
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["pass"] is False

print("PASS M02 developer tool contracts")
