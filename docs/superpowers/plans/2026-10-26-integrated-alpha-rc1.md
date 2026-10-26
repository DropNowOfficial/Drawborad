# Windows Integrated Alpha RC1 Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Preserve this bounded scope and the evidence ledger.

**Goal:** Repair release integration and produce a truthful Windows candidate if the required native environment and inputs are available; otherwise preserve executable scripts, committed fixes and exact blockers.

**Architecture:** Reuse the full-scene C++/QML/worker code. Add explicit integration gates and receipts; perform independent fixes in disjoint file scopes and review the combined result once.

**Tech Stack:** C++20, CMake, Qt 6.12/MSVC 2022, ONNX Runtime CPU, PowerShell, Python.

**Spec:** `docs/superpowers/specs/2026-10-26-integrated-alpha-rc1-design.md` (derived directly from the user's supplied execution/acceptance contract).

## Global Constraints

- Base SHA `69504ea037bf3a30c7a7afae2f19dc8742791610`; release branch `release/win11-integrated-alpha-rc1`.
- Windows 11 x64 / MSVC 2022 / Qt 6.12 / C++20; ONNX Runtime CPU first.
- Never claim historical, synthetic, Linux, diagnostics-only, missing or unregistered tests are real Windows/SAM/UI passes.
- No main merge/force push/model download/user-file replacement/CI dispatch/CUDA work/PSD scope expansion.
- SSIM >= 0.98 remains unchanged; historical 0.6487 is a failed historical measurement.

## Review Focus

- Reconfigured SDK paths must not reuse cached header/library paths from another root.
- Missing/duplicate/stale JUnit records and a changed source tree must not create a green release report.
- Cancellation, changed source image and stale document revision preserve both pending review and live document.
- A valid manifest with incompatible actual ONNX tensors must not become Ready; model-file changes invalidate its receipt.
- Extracted binaries must use packaged Release dependencies, not build-tree PATH or Debug Qt/MSVC DLLs.

### Task 1: Build profiles, dependency gates and evidence (root)

**Files:** `CMakeLists.txt`, `cmake/Dependencies.cmake`, `cmake/TestTargets.cmake`, new `cmake/ReleaseValidation.cmake`, `tools/qa/release_report.py`, `tests/build/test_release_validation.py`, `CMakePresets.json`.

**Interfaces:** Produces `TEST_INVENTORY.json`, explicit profiles/labels, and a JSON report containing `status`, `counts`, `tests`, `not_registered`, `blockers`, `git_sha`, `source_tree`, `configuration`, `profile`, and independent runtime/fidelity/oracle gates. Packaging consumes these.

- [ ] Run the exact immutable baseline's existing portable configuration and retain logs (historical evidence is excluded).
- [ ] RED: configure strict integration with missing dependencies and demand nonzero status plus all reasons; verify a nonempty invalid ORT root fails rather than disabling inference. RED: feed a result report a missing test, duplicate test, skip and mismatched SHA; none may report Passed.
- [ ] Implement explicit SDK validation/reconfiguration and per-test inventory, all-model-library DLL resolution, source/configuration identity, and robust JUnit summarization.
- [ ] GREEN: run the new Python behavioral tests; run the complete available portable CTest suite on a committed candidate snapshot. Report optional/not-registered tests separately.

### Task 2: Seven-state model readiness (model_readiness agent)

**Files:** `src/models/AdapterRegistry.*`, `OrtSession.*`, `ModelManifest.h`, `PackageVerifier.cpp`, `tests/models/ModelReadinessTests.cpp`, existing opt-in runtime tests.

**Interfaces:** `Result<ModelReadinessReport> preflightModelPackage(const PackagePath&, DevicePreference, std::stop_token)`; stable state names and exact manifest/encoder/decoder digests; worker consumes report. No new CMake source target required.

- [ ] RED: malformed optional types, tampered model, missing runtime/provider and static-only model cannot become Ready; preserve provenance.
- [ ] Implement minimal classification and actual runtime interface validation. Real smoke runs through existing encoder/decoder; retain explicit probe-kind metadata.
- [ ] GREEN: compile/run model-only portable tests; add/run actual ORT cases only if a local SDK and test models are available. Real SAM remains a separate blocked gate without its package.

### Task 3: UI lifecycle and model status (ui_workflow agent)

**Files:** `apps/studio/{QtBridge,Main}.*`, `apps/worker/Main.cpp`, `src/ui/*`, `src/platform/CanvasItem.*`, targeted `src/jobs/WinWorkerHost.cpp`, `ui/Main.qml`, `tests/studio/*`, one full-scene test receipt adaptation.

**Interfaces:** Worker task `model.preflight.v1` and structured readiness receipt; no network. UI updates only from current jobs. Render/input use a shared zoom/offset transform.

- [ ] Add regressions for stale pending-review replacement, missing/changed source pin, and generic cancellation; add actual Qt zoom/coordinate/EXIF checks where supported.
- [ ] Stage review changes atomically; route cancellation to active job; integrate model state/capability off the GUI thread; add bounded zoom controls.
- [ ] Execute available protocol/core tests. Qt/Windows tests remain Not Tested/Blocked when unavailable; never invent RED/GREEN native evidence.

### Task 4: Portable packaging and controlled workflow (windows_packaging agent)

**Files:** `tools/windows/*`, `.github/workflows/windows-alpha-portable.yml`, `tests/build/test_alpha_release_helpers.py`, existing packaging policy tests.

**Interfaces:** CLI `-QtRoot`, `-OnnxRuntimeRoot`, real coffee/package/reference paths or explicit `-UiOnly`; consumes Task 1 report/inventory.

- [ ] RED: packaging helpers reject wrong architecture, missing imports, debug dependencies and false/mismatched evidence; manual candidate workflow policy.
- [ ] Implement SDK/toolchain validation, ALL build, complete CTest, official dry-run/deploy, dependency licenses and exact provenance. Reuse only compatible caches; preserve original directories.
- [ ] Implement extracted ZIP clean-search-path smoke with real `--smoke`, worker protocol and token check; archive full SHA and checksum only after success.
- [ ] GREEN: helper tests locally; PowerShell/Windows/package launch are explicitly blocked until executed on target.

### Final integration and handoff (root)

- [ ] Read all diffs and run one independent review, fixing concrete important findings with focused regressions.
- [ ] Commit/push only the candidate branch, verify remote HEAD, retain tested source tree and exact configuration evidence.
- [ ] Update the readiness matrix and 17-step coffee checklist. Include exact Windows commands, model/input prerequisites, test reports, actual artifact locations/hashes or explicit absence.
- [ ] Deliver no fake executable ZIP, fake screenshots, inferred Photoshop pass or lifted fidelity gate.
