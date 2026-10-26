# Windows Integrated Alpha RC1: bounded release integration

## Authority and scope

This implements the user's 2026-10-26 Windows Integrated Alpha RC1 acceptance contract. The existing full-scene implementation is the product; this work connects and verifies it. Base branch: `impl/full-scene-checkpoint-20261025`; observed remote HEAD and starting SHA: `69504ea037bf3a30c7a7afae2f19dc8742791610`. Candidate branch: `release/win11-integrated-alpha-rc1`.

Required target: Windows 11 x64, MSVC 2022, Qt 6.12, C++20, ONNX Runtime CPU. No main merge, force push, user-file overwrite, silent model download, self-hosted runner installation, billing change, CUDA rewrite, fidelity shortcut, or additional PSD/PSB tag research is authorized. Preserve worker protocol, build freshness token, source-free stroke replay, source identity checks, and SSIM >= 0.98 as an unmet quality goal.

## Existing implementation and concrete defects

The repository already contains native drawing, review, full-scene stroke reconstruction, refinement, replay, .draw persistence and layered PSD export. The packaging script neither passes ONNX Runtime to CMake nor builds/runs the test targets; its diagnostics exit before Qt initialization. Optional integration tests silently disappear. Static model compatibility can report support without executing a model. The general cancel button misses workflow jobs, stale review completion can replace a pending review, and the canvas has no usable zoom.

## Approach

Use the existing code and test architecture, with four bounded changes: explicit build/test profiles and evidence, seven-state worker-side model preflight, targeted UI/lifecycle repairs, and reproducible Windows packaging. A rewrite and algorithm expansion conflict with the user's delivery priority. Hosted CI is a manual fallback only; this session does not dispatch any paid or hosted runs.

The current executor is Linux x86_64 with GCC 13.3, without Windows/MSVC/Qt/Photoshop or access to the user's Windows filesystem. Historical context names `D:\Drawborad` and `D:\Qt\6.12.0\msvc2022_64`; these paths have not been inspected in this session. The repository includes coffee mask JSON but no coffee source image, ONNX model package, or external official tensor/reference directories. These are blockers, not skipped successes. A Linux binary or source archive is never delivered as a Windows application.

## Interfaces

- `DRAWBORAD_TEST_PROFILE`: `CorePortable` or `RealWindowsIntegration`.
- Core tests use the `CorePortable` CTest label. Available Windows UI/platform tests use `WindowsUI`; real coffee tests use `RealWindowsIntegration`. Unavailable tests appear in `TEST_INVENTORY.json` with reasons. Strict integration configuration fails if required dependencies are missing.
- Required real inputs: `DRAWBORAD_ONNXRUNTIME_ROOT`, `DRAWBORAD_TEST_COFFEE_IMAGE`, `DRAWBORAD_TEST_COFFEE_PACKAGE`, `DRAWBORAD_TEST_COFFEE_REFERENCE`, `DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE`, `DRAWBORAD_TEST_FULL_SCENE_REFERENCE`. Coffee bytes must match `cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7`.
- Model preflight returns exactly `MissingModel`, `InvalidManifest`, `ModelIntegrityFailed`, `RuntimeUnavailable`, `ProviderUnavailable`, `IncompatibleModel`, or `Ready`. Cancellation is an error, not Ready. Ready requires successful real encoder and decoder execution; a deterministic smoke input is identified as such and is never coffee inference evidence.
- Worker preflight is asynchronous. A successful receipt binds exact manifest, encoder and decoder hashes; image jobs revalidate those bytes. Source provenance, model source/version/license/adapter/provider and runtime version remain inspectable.
- `tools/qa/release_report.py --build-dir ... --configuration Release --junit ... --output ...` joins one configuration's inventory and actual JUnit results. Missing/duplicate/unexecuted tests, skips, source mismatches and build mismatches cannot produce Passed.
- `Build-Alpha.ps1` defaults to strict integration; explicit `-UiOnly` is the only model-free package mode. A release package requires a clean committed source tree. SDKs and test inputs are user-supplied; no model weights enter GitHub.

## Release evidence and boundaries

Package contents include the native app and Worker, Qt deployment, ORT for integrated mode, build token, BUILD_INFO.json, TEST_RESULTS.json, KNOWN_LIMITATIONS.md, Run-Diagnostics.cmd, model instructions and actual dependency licenses. Use official windeployqt dry-run, PE/import validation, archive extraction into a new directory and clean runtime search paths. Require the official MSVC redistributable instead of copying arbitrary development DLLs. Produce the full-SHA ZIP and independent SHA-256 only when a native candidate actually exists and its smoke checks ran.

Runtime acceptance, automated structure checks, Photoshop oracle and fidelity are separate gates. Runtime Alpha stays Blocked without actual Windows application/AI workflow evidence. Historical SSIM 0.6487 remains failed against 0.98 and is not a new measurement. Photoshop remains `Blocked — Not Run` in this environment. Missing original/ROI/screenshots are recorded, never synthesized as evidence.
