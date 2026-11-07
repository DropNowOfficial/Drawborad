#!/usr/bin/env python3
"""Run a frozen automatic prototype twice per independent reference fixture."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def read_manifest(path):
    manifest = json.loads(path.read_text())
    if not isinstance(manifest, dict) or not isinstance(manifest.get("fixtures"), list) or not manifest["fixtures"]:
        raise ValueError("fixtures must be a nonempty list")
    seen = set()
    for index, fixture in enumerate(manifest["fixtures"]):
        label = f"fixture {index}"
        if not isinstance(fixture, dict):
            raise ValueError(f"{label} must be an object")
        name = fixture.get("id")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
            raise ValueError(f"{label} id must be a safe local name")
        if name in seen:
            raise ValueError(f"{label} id is duplicated: {name}")
        seen.add(name)
        if any(type(fixture.get(key)) is not int or not 1 <= fixture[key] <= 512 for key in ("width", "height")):
            raise ValueError(f"{label} dimensions must be integers in 1..512")
        masks = fixture.get("masks")
        if not isinstance(masks, list) or not 1 <= len(masks) <= 16:
            raise ValueError(f"{label} masks must contain 1..16 paths")
        if any(not isinstance(value, str) or not value.strip() or "\0" in value
               for value in [fixture.get("rgba"), *masks]):
            raise ValueError(f"{label} rgba/masks must be nonempty file paths")
    return manifest


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--native-runner", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--diagnostic-unsupported", action="store_true", help="Explicitly reproduce known unsupported-input diagnostics, not accepted paintings")
    args = p.parse_args()
    try:
        manifest = read_manifest(args.manifest)
    except (OSError, ValueError) as error:
        p.error(f"invalid manifest: {error}")
    args.output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).with_name("plan.py").resolve()
    base = args.manifest.resolve().parent
    runs, failures = [], []
    for fixture in manifest["fixtures"]:
        # QA-only ROIs/provenance are intentionally not sent to the planner.
        common = ["--experimental", "--rgba", str(base / fixture["rgba"]), "--width", str(fixture["width"]),
                  "--height", str(fixture["height"])]
        if args.diagnostic_unsupported:common.append("--diagnostic-unsupported")
        masks = [str(base / path) for path in fixture["masks"]]
        for mask in masks: common.extend(["--mask", mask])
        native_paths = []
        try:
            for repeat in (1, 2):
                plan_dir = args.output / f"{fixture['id']}-plan-{repeat}"
                native_dir = args.output / f"{fixture['id']}-native-{repeat}"
                planned = subprocess.run([sys.executable, str(source), *common, "--output", str(plan_dir)],
                                         capture_output=True, text=True, timeout=120)
                (args.output / f"{fixture['id']}-planner-{repeat}.log").write_text(planned.stdout + planned.stderr)
                if planned.returncode:
                    detail=planned.stderr.strip() or planned.stdout.strip() or f"exit {planned.returncode}"
                    raise RuntimeError(f"planner rejected: {detail.splitlines()[-1]}")
                rendered = subprocess.run([str(args.native_runner.resolve()), str(plan_dir / "plan.txt"),
                                          str(base / fixture["rgba"]), str(native_dir), *masks],
                                         capture_output=True, text=True, timeout=120)
                (args.output / f"{fixture['id']}-native-{repeat}.log").write_text(rendered.stdout + rendered.stderr)
                if rendered.returncode: raise RuntimeError(f"native rejected: {rendered.stderr.strip()}")
                native_paths.append(native_dir)
            audits = [json.loads((path / "audit.json").read_text()) for path in native_paths]
            audits[0]["repeatGeometryColorDigest"] = audits[1]["geometryColorDigest"]
            audits[0]["preflight"]=json.loads((args.output/f"{fixture['id']}-plan-1/preflight.json").read_text())
            audits[0]["plannerSourceSha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
            audits[0]["repeatNativeFinalByteIdentical"] = (native_paths[0] / "final.rgba").read_bytes() == (native_paths[1] / "final.rgba").read_bytes()
            if audits[0]["geometryColorDigest"]!=audits[1]["geometryColorDigest"] or not audits[0]["repeatNativeFinalByteIdentical"]:
                raise RuntimeError("repeat geometry/color or native pixels differ")
            (native_paths[0] / "audit.json").write_text(json.dumps(audits[0], separators=(",", ":")))
            runs.append(dict(fixture=fixture["id"], variant="automatic-stages-v6",
                             nativeOutput=str((native_paths[0] / "final.rgba").resolve()),
                             metadata=str((native_paths[0] / "audit.json").resolve())))
            print(f"EXECUTED {fixture['id']}: quality not evaluated; {len(audits[0]['events'])} native events; repeat identical={audits[0]['repeatNativeFinalByteIdentical']}", flush=True)
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(dict(fixture=fixture["id"], error=str(error)))
            print(f"BLOCKED {fixture['id']}: {error}", flush=True)
        (args.output / "runs.json").write_text(json.dumps(dict(scope="execution-only; not art-quality acceptance", runs=runs, failures=failures), indent=2))
    return 1 if failures else 0


if __name__ == "__main__": raise SystemExit(main())
