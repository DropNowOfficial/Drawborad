import pathlib
import sys

if len(sys.argv) != 3:
    print("usage: test_build_freshness.py EXPECTED BUILT", file=sys.stderr)
    raise SystemExit(2)

expected_path = pathlib.Path(sys.argv[1])
built_path = pathlib.Path(sys.argv[2])

if not expected_path.is_file():
    print(f"missing expected build token: {expected_path}", file=sys.stderr)
    raise SystemExit(1)
if not built_path.is_file():
    print(f"missing successful-build token: {built_path}", file=sys.stderr)
    raise SystemExit(1)

expected = expected_path.read_text(encoding="utf-8").strip()
built = built_path.read_text(encoding="utf-8").strip()

if not expected or expected != built:
    print("Drawborad.exe is stale: the current configure step was not followed by a successful desktop build.", file=sys.stderr)
    raise SystemExit(1)

print("PASS current Drawborad desktop build token")
