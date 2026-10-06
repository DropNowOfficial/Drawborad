import pathlib
import struct
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    print("usage: test_codec_worker.py BUILD_DIR", file=sys.stderr)
    raise SystemExit(2)

build = pathlib.Path(sys.argv[1])
names = ["DrawboradCodec.exe", "DrawboradCodec"]
candidates = []
for name in names:
    candidates.extend(build.rglob(name))
if not candidates:
    print("missing DrawboradCodec executable", file=sys.stderr)
    raise SystemExit(1)
exe = candidates[0]

# 1x1, 24-bit uncompressed BMP. Pixel is BGR=(3,2,1).
bmp = bytearray()
bmp += b"BM"
bmp += struct.pack("<IHHI", 58, 0, 0, 54)
bmp += struct.pack("<IIIHHIIIIII", 40, 1, 1, 1, 24, 0, 4, 2835, 2835, 0, 0)
bmp += bytes([3, 2, 1, 0])

with tempfile.TemporaryDirectory() as td:
    td = pathlib.Path(td)
    src = td / "one.bmp"
    out = td / "payload.bin"
    src.write_bytes(bmp)
    run = subprocess.run(
        [str(exe), "--decode", str(src), str(out)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=20,
    )
    if run.returncode != 0:
        print(run.stdout)
        print(run.stderr, file=sys.stderr)
        raise SystemExit(run.returncode or 1)
    data = out.read_bytes()
    if len(data) < 8:
        print("codec payload too short", file=sys.stderr)
        raise SystemExit(1)
    length = struct.unpack_from("<Q", data, 0)[0]
    magic = data[8:8 + length]
    if magic != b"DRAWBORAD-CODEC-1":
        print(f"unexpected payload magic: {magic!r}", file=sys.stderr)
        raise SystemExit(1)

print("PASS isolated codec worker produced bounded codec payload")
