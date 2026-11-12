"""Real native bucket/save-copy regression. Requires a built engine, no mocks."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    subprocess.run([binary, "smoke", "--output", str(root / "generated")],
                   check=True, capture_output=True)
    proc = subprocess.Popen([binary, "session"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True)
    seq = 0
    def call(method, **params):
        global seq
        seq += 1
        proc.stdin.write(json.dumps(dict(request_id=str(seq), method=method, params=params))+"\n")
        proc.stdin.flush()
        result = json.loads(proc.stdout.readline())
        assert result["request_id"] == str(seq)
        return result
    def ok(method, **params):
        result = call(method, **params)
        assert "result" in result, result
        return result["result"]
    try:
        original = root / "generated/project.draw"
        original_bytes = original.read_bytes()
        state = ok("open", path=str(original))
        old_ids = {n["id"] for n in state["nodes"]}
        state = ok("command", type="add_fill_layer", expected_revision=state["revision"])
        layer = next(n["id"] for n in state["nodes"] if n["id"] not in old_ids)
        ok("export", path=str(root/"before.png"), format="png")
        state = ok("command", type="bucket_fill", layer_id=layer, x=0, y=0,
                   tolerance=255, color="#ff00ff", expected_revision=state["revision"])
        assert state["strokes"][-1]["tool"] == "bucket"
        ok("export", path=str(root/"after.png"), format="png")
        assert (root/"before.png").read_bytes() != (root/"after.png").read_bytes()
        unchanged = call("save", expected_revision=state["revision"])
        assert "error" in unchanged
        assert original.read_bytes() == original_bytes
        state = ok("undo", expected_revision=state["revision"])
        ok("export", path=str(root/"undo.png"), format="png")
        assert (root/"before.png").read_bytes() == (root/"undo.png").read_bytes()
        state = ok("redo", expected_revision=state["revision"])
        copy = root/"separate.draw"
        saved = ok("save_copy", path=str(copy), expected_revision=state["revision"])
        assert not saved["dirty"] and original.read_bytes() == original_bytes
        rejected = call("save_copy", path=str(copy), expected_revision=saved["revision"])
        assert "error" in rejected
        ok("close", expected_revision=saved["revision"])
        reopened = ok("open", path=str(copy))
        assert reopened["strokes"] == saved["strokes"]
        ok("export", path=str(root/"reopened.png"), format="png")
        assert (root/"after.png").read_bytes() == (root/"reopened.png").read_bytes()
        ok("export", path=str(root/"layers.ora"), format="ora")
        print("PASS native bucket/render/undo/redo/separate-save/reopen/export")
    finally:
        proc.stdin.close()
        proc.wait(timeout=10)
