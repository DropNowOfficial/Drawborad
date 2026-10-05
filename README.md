# Drawborad

Native Windows drawing studio — **F01–F06 foundation implementation**.

This branch contains C++20 document, transaction, persistence, worker and native-shell
code. It is **not a completed drawing application or a qualified Windows release**.
No browser/WebView is used. Reconstruction, painting/rendering, PSD codecs, ICC
conversion and the full desktop experience belong to the later approved plans.

## Current evidence

On the available Linux host, Debug, Release and ASan/UBSan each ran **8 passing
CTest entries, 0 failures and 2 explicitly skipped Windows-only entries**.
This comprises 50 native C++ cases, 16 Python unit cases and a real worker-process
protocol probe. A 10,000-command edit/undo/redo test is included.

Windows SDK, Qt, PowerShell and the target NVIDIA machine were unavailable.
Therefore real Windows dependency qualification, native pipe/Job Object testing,
and Qt/D3D11 WARP/hardware launch remain **Blocked**, not passed.

See [implementation report](reports/foundation/implementation-report.md),
[test summary](reports/foundation/test-summary.json), and the
[approved foundation plan](docs/superpowers/plans/2026-10-04-drawborad-01-foundation.md).

## Lab build (no vendor download)

Prerequisites: a C++20 compiler, CMake >= 3.25, Ninja and Python 3.9+ for development
checks. Actual lab tools are recorded in `reports/foundation/lab-toolchain.json`.
Python is not a runtime dependency of the native application.

```sh
cmake --preset lab-debug
cmake --build --preset lab-debug
ctest --preset lab-debug --no-tests=error --output-on-failure

cmake --preset lab-release
cmake --build --preset lab-release
ctest --preset lab-release --no-tests=error --output-on-failure

cmake --preset lab-asan
cmake --build --preset lab-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset lab-asan --output-on-failure

./out/build/lab-debug/DrawboradDiagnostics --diagnostics device.json
```

Lab targets are `drawborad_core`, `DrawboradWorker`, `DrawboradDiagnostics` and tests.
A Linux executable must not be renamed `.exe` and presented as a Windows build.

## Windows qualification (not yet performed)

Use an x64 MSVC developer environment, the approved Windows SDK and the exact Qt
package declared in `cmake/Dependencies.cmake`. The release manifest is deliberately
fail-closed. Its current empty artifact list is **not** a qualified dependency lock.
Populate it only from downloaded/build-produced bytes and real compatibility evidence;
do not fabricate hashes or change its status simply to bypass the gate.

```powershell
powershell -File tools/bootstrap.ps1 -Mode Verify -Cache .deps
cmake --preset win-debug
cmake --build --preset win-debug
ctest --preset win-debug --no-tests=error --output-on-failure
# Real Qt/Direct3D smoke, once the prerequisites above have passed:
.\out\build\win-debug\Drawborad.exe --smoke --warp --diagnostics warp.json
.\out\build\win-debug\Drawborad.exe --smoke --diagnostics hardware.json
```

The qualified desktop target uses Qt Quick and a native Windows window at 1280x800.
It opens a genuinely empty workspace, creates/opens/saves documents, exposes errors
without replacing a valid document, and demonstrates a separately cancellable native
worker. The paper rectangle is explicitly a foundation placeholder, not a painting
renderer or a generated artwork.

## Modules

- `src/core`: checked sizes, RGB/CMYK descriptors, immutable snapshots, objects and
  stage layers, commands and inverse transactions.
- `src/storage`: SHA-256 tiles, bounded binary deltas, checksummed journals, atomic
  saves/backups, single-writer leases and history budgeting.
- `src/jobs`: versioned/nonce-bound IPC, cancellation, stale-result rejection and
  shared resource reservations. The worker implements `native.smoke`, not AI.
- `src/platform`: real device/OS diagnostics; absent hardware is reported as absent.
- `apps/studio`, `ui`: native shell and a controller without QML in the core.

The v1 binary format stores authoritative resources and event data; it does not run
models when opening a project. Resource payloads are capped at 64 MiB, project records
at 512 MiB, IPC control messages at 1 MiB, and worker snapshot mappings at 64 MiB.
These are explicit foundation safety limits, not claims of production 8K capacity.

## Development rules

Keep `main` unchanged until review. Do not commit model weights, customer artwork,
SDK/DLL distributions or build caches. No telemetry, automatic downloads, image APIs,
GPU-performance claims, Photoshop-interoperability claims or release installer are
included in this foundation milestone. Follow the approved plans and preserve the
recorded platform blockers before moving to subsequent modules.
