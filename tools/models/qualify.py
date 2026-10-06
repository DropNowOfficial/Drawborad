#!/usr/bin/env python3
"""Validate recorded SAM 2.1 qualification evidence.

This tool only reads local JSON files. It never downloads models or fixtures.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Qualify recorded SAM 2.1 fixture results.")
    p.add_argument("--recipe", type=pathlib.Path, required=True)
    p.add_argument("--fixtures", type=pathlib.Path, required=True)
    p.add_argument("--report", type=pathlib.Path, required=True)
    return p


def load_object(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def is_hex(value: object, length: int) -> bool:
    return (
        isinstance(value, str)
        and len(value) == length
        and all(c in "0123456789abcdefABCDEF" for c in value)
    )


def evaluate(recipe: dict, fixtures: dict) -> dict:
    failures: list[str] = []
    if recipe.get("taskId") != "sam21.image.v1":
        failures.append("recipe.taskId")
    if recipe.get("variant") not in ("tiny", "small"):
        failures.append("recipe.variant")

    threshold = float(recipe.get("minMaskIou", 0.995))
    gpu_limit = int(recipe.get("maxPeakGpuBytes", 0))
    cases = fixtures.get("cases")
    if not isinstance(cases, list) or len(cases) < 20:
        failures.append("fixtures.caseCount")
        cases = [] if not isinstance(cases, list) else cases

    source_commit = fixtures.get("sourceCommit")
    checkpoint_hash = fixtures.get("checkpointSha256")
    if not is_hex(source_commit, 40):
        failures.append("fixtures.sourceCommit")
    if not is_hex(checkpoint_hash, 64):
        failures.append("fixtures.checkpointSha256")

    network_attempts = fixtures.get("networkAttempts")
    if network_attempts != 0:
        failures.append("fixtures.networkAttempts")

    minimum_iou = 1.0
    for index, case in enumerate(cases):
        if not isinstance(case, dict):
            failures.append(f"cases[{index}]")
            continue
        try:
            iou = float(case["maskIou"])
        except (KeyError, TypeError, ValueError):
            failures.append(f"cases[{index}].maskIou")
            continue
        minimum_iou = min(minimum_iou, iou)
        if iou < threshold:
            failures.append(f"cases[{index}].maskIou")
        cpu_shape = case.get("cpuShape")
        cuda_shape = case.get("cudaShape")
        if cuda_shape is not None and cpu_shape != cuda_shape:
            failures.append(f"cases[{index}].shapeParity")

    peak_gpu = fixtures.get("peakGpuBytes")
    if gpu_limit:
        if not isinstance(peak_gpu, int) or peak_gpu < 0 or peak_gpu > gpu_limit:
            failures.append("fixtures.peakGpuBytes")

    report = {
        "schema": 1,
        "taskId": recipe.get("taskId"),
        "variant": recipe.get("variant"),
        "caseCount": len(cases),
        "minMaskIouRequired": threshold,
        "minMaskIouObserved": minimum_iou if cases else None,
        "networkAttempts": network_attempts,
        "peakGpuBytes": peak_gpu,
        "maxPeakGpuBytes": gpu_limit,
        "sourceCommit": source_commit,
        "checkpointSha256": checkpoint_hash,
        "failures": sorted(set(failures)),
    }
    report["pass"] = not report["failures"]
    return report


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        recipe = load_object(args.recipe)
        fixtures = load_object(args.fixtures)
        report = evaluate(recipe, fixtures)
    except (ValueError, TypeError) as exc:
        report = {"schema": 1, "pass": False, "failures": [str(exc)]}

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0 if report.get("pass") else 2


if __name__ == "__main__":
    raise SystemExit(main())
