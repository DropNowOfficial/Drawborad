#!/usr/bin/env python3
"""Validate Windows deployment evidence without guessing missing dependencies.

DUMPBIN and windeployqt are the native authorities. Recorded-output commands let
the failure rules run on a non-Windows host; they do not qualify a Windows build.
"""
from pathlib import Path, PurePosixPath, PureWindowsPath
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys


# Only Windows components and the documented VC++ prerequisite may resolve from
# System32. A random third-party DLL installed there must not hide a package gap.
WINDOWS_DLLS = set("""
advapi32 authz avrt bcrypt bcryptprimitives cabinet cfgmgr32 clbcatq combase comctl32
comdlg32 crypt32 cryptbase cryptnet cryptsp d2d1 d3d11 d3d12 d3dcompiler_47 dbghelp dcomp
dhcpcsvc dhcpcsvc6 dnsapi dwrite dwmapi dxcore dxgi dxva2 gdi32 gdi32full glu32
hid imm32 iphlpapi kernel32 kernelbase mpr msasn1 msimg32 msvcrt ncrypt netapi32
normaliz nsi ntdll ole32 oleacc oleaut32 opengl32 pdh powrprof profapi propsys psapi
rpcrt4 runtimeobject sechost secur32 setupapi shcore shell32 shlwapi sspicli ucrtbase uiautomationcore user32
userenv usp10 uxtheme version wevtapi windowscodecs winhttp wininet winmm winspool
wintrust winnsi winsta wlanapi wldap32 ws2_32 wsock32 wtsapi32
""".split())
VC_RUNTIME = re.compile(r"^(?:msvcp|msvcr|vcruntime|concrt|vcomp)\d+(?:_\d+|_atomic_wait|_codecvt_ids)?\.dll$", re.I)
DEBUG_RUNTIME = re.compile(r"^(?:(?:msvcp|msvcr|vcruntime|concrt|vcomp)\d+(?:_\d+)?d|ucrtbased|mfc\d+u?d)\.dll$", re.I)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def x64_machines(headers):
    values = re.findall(r"^\s*([0-9a-f]{3,4})\s+machine\b", headers, re.I | re.M)
    values += re.findall(r"^\s*Machine\s*:\s*([0-9a-f]{3,4})\b", headers, re.I | re.M)
    machines = sorted({value.lower() for value in values})
    if not machines:
        raise ValueError("DUMPBIN output contains no recognized machine header")
    if machines != ["8664"]:
        raise ValueError("x64 (8664) required for every member; found " + ", ".join(machines))
    return machines


def imports(dependents):
    if "File Type:" not in dependents or "Summary" not in dependents:
        raise ValueError("Unrecognized DUMPBIN /DEPENDENTS output")
    names, in_imports = set(), False
    for line in dependents.splitlines():
        entry = line.strip()
        if re.fullmatch(r"Image has the following (?:delay load )?dependencies:", entry, re.I):
            in_imports = True
        elif entry == "Summary":
            in_imports = False
        elif in_imports and entry:
            if PureWindowsPath(entry).name != entry or Path(entry).suffix.casefold() not in (".dll", ".drv", ".ocx", ".exe"):
                raise ValueError("Unrecognized import entry: " + entry)
            names.add(entry)
    return sorted(names, key=str.casefold)


def is_debug(name):
    name = name.casefold()
    # Qt6Quick3D.dll is a release module; its debug variant ends in 3Dd.dll.
    return bool(DEBUG_RUNTIME.match(name)) or (
        name.startswith("qt6") and name.endswith("d.dll") and not name.endswith("3d.dll"))


def normalized_target(raw, root):
    value = str(raw).replace("\\", "/")
    path = Path(value)
    if PureWindowsPath(value).drive and os.name != "nt":
        raise ValueError("A Windows absolute deployment target requires a Windows host")
    if not path.is_absolute():
        path = root / path
    try:
        result = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError("Deployment target is outside the package: " + value) from exc
    if result == ".":
        raise ValueError("Deployment target must name a file")
    return result


def deployment_files(root, plan=None):
    root = Path(root).resolve()
    files = {}
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.casefold() in (".exe", ".dll"):
            target = path.relative_to(root).as_posix()
            files[target.casefold()] = {"source": str(path), "target": target}
    if plan is not None:
        if not isinstance(plan, dict) or not isinstance(plan.get("files"), list):
            raise ValueError("windeployqt JSON must contain a files list")
        for entry in plan["files"]:
            if not isinstance(entry, dict) or not entry.get("source") or not entry.get("target"):
                raise ValueError("windeployqt file entry must contain source and target")
            target = normalized_target(entry["target"], root)
            source = Path(entry["source"]).resolve()
            if not source.is_file():
                raise ValueError("Planned deployment source is missing: " + str(source))
            if source.suffix.casefold() not in (".exe", ".dll"):
                continue
            key = target.casefold()
            old = files.get(key)
            if old and Path(old["source"]).resolve() != source and sha256(old["source"]) != sha256(source):
                raise ValueError("Conflicting deployment sources for " + target)
            files[key] = {"source": str(source), "target": target}
    if not files:
        raise ValueError("No EXE or DLL files to audit")
    return [files[key] for key in sorted(files)]


def audit_records(records, system_dir):
    if not records:
        raise ValueError("No native file records to audit")
    system_dir = Path(system_dir)
    system = {path.name.casefold(): path for path in system_dir.iterdir() if path.is_file()}
    packaged = {record["target"].replace("\\", "/").casefold() for record in records}
    errors, files = [], []
    for record in records:
        target = record["target"].replace("\\", "/")
        entry = {"target": target, "source": record["source"], "dependencies": []}
        try:
            entry["machines"] = x64_machines(record["headers"])
            entry["sha256"] = sha256(record["source"])
            if is_debug(PurePosixPath(target).name):
                errors.append(target + ": debug runtime is prohibited")
            for dependency in imports(record["dependents"]):
                name = dependency.casefold()
                resolution = None
                if is_debug(name):
                    errors.append(target + ": debug runtime import " + dependency)
                elif name in packaged:
                    resolution = "package/" + name
                elif str(PurePosixPath(target).parent / name).casefold() in packaged:
                    resolution = "package/" + str(PurePosixPath(target).parent / name)
                elif name.startswith(("api-ms-win-", "ext-ms-win-")):
                    resolution = "Windows API set"
                elif (name.rsplit('.', 1)[0] in WINDOWS_DLLS or VC_RUNTIME.match(name)) and name in system:
                    resolution = str(system[name])
                else:
                    errors.append(target + ": unresolved dependency " + dependency)
                entry["dependencies"].append({"name": dependency, "resolved": resolution})
        except (KeyError, OSError, ValueError) as exc:
            errors.append(target + ": " + str(exc))
        files.append(entry)
    return {"status": "Failed" if errors else "Passed", "files": files, "errors": errors,
            "scope": "Recorded PE imports and x64 headers; dynamic loads require extracted-package smoke"}


def dumpbin(executable, option, binary):
    result = subprocess.run([str(executable), "/nologo", option, str(binary)],
                            capture_output=True, text=True, errors="replace", timeout=60)
    if result.returncode:
        raise ValueError("DUMPBIN failed for " + str(binary) + ": " + result.stdout + result.stderr)
    return result.stdout


def native_audit(root, plan, executable, system_dir, log_dir):
    log_dir = Path(log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for index, entry in enumerate(deployment_files(root, plan)):
        record = dict(entry)
        for field, option in (("headers", "/headers"), ("dependents", "/dependents")):
            record[field] = dumpbin(executable, option, entry["source"])
            (log_dir / (str(index).zfill(4) + "-" + field + ".txt")).write_text(record[field], encoding="utf-8")
        records.append(record)
    (log_dir / "inputs.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    return audit_records(records, system_dir)


def cache_check(path, expected):
    path = Path(path)
    if not path.exists():
        return {"status": "Passed", "reused": False}
    actual = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^([^/#][^:]*):[^=]*=(.*)$", line)
        if match:
            actual[match[1]] = match[2]
    mismatches = []
    for key, wanted in expected.items():
        found = actual.get(key, "")
        normalize = lambda value: str(value).replace("\\", "/").rstrip("/").casefold()
        if normalize(found) != normalize(wanted):
            mismatches.append(key + ": cached=" + repr(found) + ", requested=" + repr(wanted))
    if mismatches:
        raise ValueError("Incompatible CMake cache; choose a different -BuildDirectory. " + "; ".join(mismatches))
    return {"status": "Passed", "reused": True}


def clean_environment(environment, root):
    removed = ("QT_", "QML", "QSG_", "QMAKE")
    clean = {key: value for key, value in environment.items()
             if not key.upper().startswith(removed)
             and key.upper() not in ("PATH", "QTDIR", "QT_ROOT_DIR", "CMAKE_PREFIX_PATH", "PYTHONHOME", "PYTHONPATH")}
    windows = next((value for key, value in environment.items() if key.upper() == "SYSTEMROOT"), "")
    if not windows:
        raise ValueError("SystemRoot is required for a clean Windows smoke environment")
    clean["PATH"] = ";".join((str(root), windows.rstrip("/\\") + "/System32", windows))
    return clean


def codec_sdk_check(root, toolchain):
    scope = "Preinstalled SDK file presence and hashes only; not native qualification"
    if not root or not toolchain:
        return {"status": "Blocked", "scope": scope, "files": [],
                "blockers": ["Supply the preinstalled native SDK and CMake toolchain paths on this runner; no dependencies will be downloaded"]}
    triplet = Path(root).resolve() / "x64-windows"
    required = ["include/lcms2.h", "lib/lcms2.lib", "share/lcms/copyright",
                "include/png.h", "lib/libpng16.lib", "share/libpng/copyright",
                "include/jpeglib.h", "lib/jpeg.lib", "share/libjpeg-turbo/copyright",
                "include/tiffio.h", "lib/tiff.lib", "share/tiff/copyright",
                "include/webp/encode.h", "include/webp/decode.h", "include/webp/mux.h",
                "lib/webp.lib", "lib/webpmux.lib", "share/libwebp/copyright",
                "include/zlib.h", "lib/zlib.lib", "share/zlib/copyright"]
    paths = [triplet / name for name in required] + [Path(root).resolve() / "vcpkg/status", Path(toolchain).resolve()]
    blockers = ["Missing or empty preinstalled SDK input: " + str(path)
                for path in paths if not path.is_file() or path.stat().st_size == 0]
    runtimes = sorted((triplet / "bin").glob("*.dll"))
    if not runtimes:
        blockers.append("Missing preinstalled release runtime DLLs: " + str(triplet / "bin"))
    files = [] if blockers else [{"path": str(path), "sha256": sha256(path)} for path in paths + runtimes]
    return {"status": "Blocked" if blockers else "Passed", "scope": scope,
            "root": str(Path(root).resolve()), "toolchain": str(Path(toolchain).resolve()),
            "files": files, "blockers": blockers}


def package_smoke(root, log_dir, worker_probe, native_worker_probe, expected_token):
    if os.name != "nt":
        return {"status": "Blocked", "error": "Extracted package smoke requires Windows"}
    root, log_dir = Path(root).resolve(), Path(log_dir).resolve()
    log_dir.mkdir(parents=True, exist_ok=True)
    environment = clean_environment(os.environ, root)
    expected = Path(expected_token).read_text(encoding="utf-8-sig").strip()
    actual = (root / "drawborad_build_token.txt").read_text(encoding="utf-8-sig").strip()
    if not expected or expected != actual:
        raise ValueError("Extracted package build token does not match the configured build")
    checks = []
    for name, arguments in (("warp", ["--warp"]), ("default-adapter", [])):
        report = log_dir / (name + ".json")
        command = [str(root / "Drawborad.exe"), "--smoke", "--diagnostics", str(report), *arguments]
        run = subprocess.run(command, env=environment, cwd=root, capture_output=True,
                             text=True, errors="replace", timeout=45)
        (log_dir / (name + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
        if run.returncode:
            raise ValueError(name + " GUI smoke failed with exit " + str(run.returncode))
        diagnostic = read_json(report)
        if (diagnostic.get("sceneGraph") != "Direct3D11" or not diagnostic.get("activeRenderLuid")
                or not diagnostic.get("moduleScanCompleted") or diagnostic.get("webviewModules")):
            raise ValueError(name + " GUI diagnostics did not satisfy the native smoke contract")
        checks.append({"name": name, "status": "Passed", "diagnostics": diagnostic})
    probes = [("worker-protocol", [sys.executable, str(Path(worker_probe).resolve()), str(root / "DrawboradWorker.exe")]),
              ("worker-native-transport", [str(Path(native_worker_probe).resolve()), str(root / "DrawboradWorker.exe")])]
    for name, command in probes:
        run = subprocess.run(command, env=environment, cwd=root, capture_output=True,
                             text=True, errors="replace", timeout=40)
        (log_dir / (name + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
        if run.returncode:
            raise ValueError(name + " smoke failed with exit " + str(run.returncode))
        checks.append({"name": name, "status": "Passed"})
    return {"status": "Passed", "checks": checks, "path": environment["PATH"],
            "build_token_sha256": sha256(root / "drawborad_build_token.txt"),
            "runtime_alpha": "Blocked", "scope": "Startup and worker transport only; no inference or fidelity acceptance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("machine", "audit-records", "plan", "audit", "check-cache", "clean-environment", "codec-sdk", "smoke"))
    parser.add_argument("--input")
    parser.add_argument("--root")
    parser.add_argument("--system-dir")
    parser.add_argument("--dumpbin")
    parser.add_argument("--binary")
    parser.add_argument("--log-dir")
    parser.add_argument("--cache")
    parser.add_argument("--worker-probe")
    parser.add_argument("--native-worker-probe")
    parser.add_argument("--expected-token")
    parser.add_argument("--output")
    args = parser.parse_args()
    try:
        value = read_json(args.input) if args.input else None
        if args.command == "machine":
            headers = dumpbin(args.dumpbin, "/headers", args.binary) if args.binary else value["headers"]
            result = {"status": "Passed", "machines": x64_machines(headers)}
            if args.binary:
                result.update({"file": str(Path(args.binary).resolve()), "sha256": sha256(args.binary)})
        elif args.command == "audit-records":
            result = audit_records(value, args.system_dir)
        elif args.command == "plan":
            result = {"status": "Passed", "files": deployment_files(args.root, value)}
        elif args.command == "audit":
            result = native_audit(args.root, value, args.dumpbin, args.system_dir, args.log_dir)
        elif args.command == "clean-environment":
            result = {"status": "Passed", "environment": clean_environment(value, args.root)}
        elif args.command == "codec-sdk":
            request = value or {}
            result = codec_sdk_check(request.get("root"), request.get("toolchain"))
        elif args.command == "smoke":
            result = package_smoke(args.root, args.log_dir, args.worker_probe, args.native_worker_probe, args.expected_token)
        else:
            result = cache_check(args.cache, value)
    except (KeyError, OSError, TypeError, ValueError, subprocess.TimeoutExpired) as exc:
        result = {"status": "Failed", "error": str(exc)}
    text = json.dumps(result, indent=2)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0 if result["status"] == "Passed" else 1


if __name__ == "__main__":
    sys.exit(main())
