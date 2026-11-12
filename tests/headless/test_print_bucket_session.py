"""Prospective real-engine print-sized PNG/bucket smoke, no mocks.

1725 x 2625 at 300 DPI is an engineering target, NOT a confirmed assignment
requirement. Requires the merged-only render path. Streaming ORA must preserve actual independent layers, native size and DPI.
Run: python tests/headless/test_print_bucket_session.py /path/to/drawborad-engine
"""
import hashlib
import json
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile

from PIL import Image
from ora_evidence import inspect_ora

SIZE = (1725, 2625)


def inspect_png(path, *, filled=False):
    with Image.open(path) as image:
        assert image.format == "PNG" and image.size == SIZE, (image.format, image.size)
        dpi = image.info.get("dpi")
        assert dpi and len(dpi) == 2 and all(abs(v - 300) < .05 for v in dpi), dpi
        rgba = image.convert("RGBA")
        corners = [rgba.getpixel(point) for point in
                   ((0, 0), (SIZE[0]-1, 0), (0, SIZE[1]-1), (SIZE[0]-1, SIZE[1]-1))]
        if filled:
            assert corners == [(0, 0, 0, 255)] * 4, corners
            assert rgba.getextrema() == ((0, 0), (0, 0), (0, 0), (255, 255))
        return hashlib.sha256(rgba.tobytes()).hexdigest()


def main(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        proc = subprocess.Popen([str(Path(binary).resolve()), "session"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        sequence = 0

        def call(method, **params):
            nonlocal sequence
            sequence += 1
            proc.stdin.write(json.dumps(dict(request_id=str(sequence), method=method, params=params)) + "\n")
            proc.stdin.flush()
            with selectors.DefaultSelector() as selector:
                selector.register(proc.stdout, selectors.EVENT_READ)
                assert selector.select(120), f"Native {method} timed out"
            line = proc.stdout.readline()
            assert line, f"Native {method} disconnected"
            result = json.loads(line)
            assert result["request_id"] == str(sequence), result
            return result

        def ok(method, **params):
            result = call(method, **params)
            assert "result" in result, result
            return result["result"]

        def export_png(name, *, filled=False):
            path = root / name
            ok("export", path=str(path), format="png")
            return inspect_png(path, filled=filled)

        try:
            state = ok("create", width=str(SIZE[0]), height=str(SIZE[1]))
            assert (int(state["width"]), int(state["height"])) == SIZE
            ids = {node["id"] for node in state["nodes"]}
            state = ok("command", type="add_fill_layer", expected_revision=state["revision"])
            added = [node for node in state["nodes"] if node["id"] not in ids]
            assert len(added) == 1 and added[0]["kind"] == 3
            layer = added[0]["id"]
            before = export_png("before.png")
            state = ok("command", type="bucket_fill", layer_id=layer,
                       x=SIZE[0]//2, y=SIZE[1]//2, tolerance=255, color="#000000",
                       expected_revision=state["revision"])
            assert state["strokes"][-1]["tool"] == "bucket"
            assert state["strokes"][-1]["layer_id"] == layer
            after = export_png("filled.png", filled=True)
            assert after != before
            state = ok("undo", expected_revision=state["revision"])
            assert export_png("undo.png") == before
            state = ok("redo", expected_revision=state["revision"])
            assert export_png("redo.png", filled=True) == after
            copy = root / "print-copy.draw"
            saved = ok("save_copy", path=str(copy), expected_revision=state["revision"])
            assert copy.is_file() and not saved["dirty"]
            ok("close", expected_revision=saved["revision"])
            reopened = ok("open", path=str(copy))
            assert reopened["strokes"] == saved["strokes"]
            assert export_png("reopened.png", filled=True) == after
            ora = root / "print-layers.ora"
            ok("export", path=str(ora), format="ora")
            with Image.open(root / "reopened.png") as png:
                ora_evidence = inspect_ora(ora, png.convert("RGBA").tobytes(), SIZE, require_fill=True)
            assert export_png("after-ora.png", filled=True) == after
            print(json.dumps({"test": "print-sized-native-bucket", "size": SIZE,
                              "dpi": 300, "png_undo_redo_reopen": "passed",
                              "rgba_sha256": after, "ora": ora_evidence,
                              "assignment_size_confirmed": False}))
        finally:
            proc.stdin.close()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: test_print_bucket_session.py /path/to/native-engine")
    main(sys.argv[1])
