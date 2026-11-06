# Linux Headless Engine P0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run the existing Drawborad C++ review/reconstruction/rendering/storage pipeline on Linux without Qt Quick or Windows-only dependencies, producing a real `.draw` project, zero/middle/final PNG frames, and an independently layered OpenRaster export.

**Architecture:** Keep `DocumentSnapshot` and `.draw` authoritative. Reuse `SemanticManifestImport`, `ReviewSession`, `runReconstructionJob`, `reconstructionCommand`, `renderPaintedDocument`, `ProjectStore`, and `ExportService`; add a small headless orchestration API and CLI rather than duplicating domain logic in Python or JavaScript. Qt Core/Gui are permitted for the existing JSON/image/file adapters; Qt Quick, D3D11, Win32, and the desktop shell are not part of this Linux target.

**Tech Stack:** C++20, CMake 3.25+, Qt 6 Core/Gui (Linux minimum selected for the Ubuntu 24.04 build), existing codecs/reconstruction modules, Python only for integration assertions, GitHub Actions Ubuntu 24.04.

**Spec:** `docs/superpowers/specs/2026-10-29-linux-cloud-architecture-design.md` (reviewed 2026-10-31). Source branch: `work/cloud-agent-runtime-20261028`, base SHA `a6219664c21a391ed4538ef78ccb2ae314b497be`.

## Global Constraints

- Work only on `work/linux-headless-engine-20261104`; never modify `main`, merge old Draft PRs, force-push, or publish a public service.
- Linux cloud-first; Windows desktop remains a later adapter and is not a P0 prerequisite.
- C++ `DocumentSnapshot` is the only authoritative drawing state; `.draw` is the persisted project; do not create a second Python/JavaScript project or stroke model.
- CPU mode must build and run without ONNX Runtime, CUDA, model weights, or network access.
- A real SAM 3 manifest must preserve its `sam3-image` provenance. The integration smoke fixture must be labelled `fixture-no-ai`; it is not model inference or an accuracy measurement.
- Object masks must be validated and explicitly accepted; no silent object acceptance, synthetic fallback, hidden source-image compositing, or source-image residual events.
- Rendered PNG/ORA output must be derived from saved brush events. Replay after removing the original source file must still work.
- Runtime, fidelity, layer integrity, natural-process, Photoshop, and model-inference evidence remain separate. P0 does not claim SSIM >= 0.98, Photoshop compatibility, or real SAM 3 accuracy.
- Preserve ICC profile identity/bytes through project creation and raster export when present; do not silently convert color.
- Tests must use real C++ modules and hand-checked fixture data. New behavior follows RED → GREEN.

## Review Focus

- Linux UTF-8 paths and paths containing spaces must not fail during image decode, project save, or export.
- An embedded ICC profile must be retained as a Profile resource and match the decoded source profile; missing/mismatched bytes must fail explicitly.
- Empty, overlapping, out-of-bounds, wrong-sized, or wrong-source-hash masks must never become accepted object layers.
- Hidden parent objects and mask alpha must affect exported ORA leaf pixels; group boundary records must not become blank raster layers.
- A cancelled reconstruction or stale result must not partially mutate the document or leave a success report.
- Event limits at zero, an intermediate event, and the final event must produce deterministic frames; replay must not read the original source asset.

---

### Task 1: Linux headless build profile and executable target

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/Dependencies.cmake`
- Modify: `cmake/TestTargets.cmake`
- Create: `apps/headless/Main.cpp`
- Create: `tests/headless/test_headless_cli.py`
- Create: `.github/workflows/linux-headless-p0.yml`

**Interfaces:**
- Configure option: `DRAWBORAD_HEADLESS_WORKFLOW=ON`.
- Target: `DrawboradHeadless`.
- The headless build links existing workflow sources against Qt Core/Gui only. Desktop mode retains its Windows/MSVC/Qt 6.12 checks.
- CLI contract: `DrawboradHeadless --help` exits 0 and lists `run` and `smoke`; `smoke --output DIR` is added in Task 4.

- [ ] **Step 1: Write a failing CLI target test.** Create `tests/headless/test_headless_cli.py`; it receives the executable path and asserts `--help` exits 0 and includes the two supported subcommands.
- [ ] **Step 2: Verify RED.** Run the test against the configured target path before the target exists; it must fail specifically because the executable is absent.
- [ ] **Step 3: Add the headless CMake profile.** Find Qt 6 Core/Gui on Linux, build `drawborad_workflow` outside `DRAWBORAD_DESKTOP`, and add `DrawboradHeadless`. Do not link Qt Quick, D3D11, DXGI, or Windows-only UI code.
- [ ] **Step 4: Implement CLI help only.** Keep command parsing small and do not add model or drawing behavior in this task.
- [ ] **Step 5: Verify GREEN.** Configure/build on Ubuntu 24.04, run the CLI test, and run the portable CTest suite. Retain the exact Qt version, CMake cache, Git SHA, and test inventory.
- [ ] **Step 6: Commit Task 1.**

### Task 2: Headless accepted-object → real stroke pipeline

**Files:**
- Create: `src/headless/HeadlessWorkflow.h`
- Create: `src/headless/HeadlessWorkflow.cpp`
- Modify: `src/ui/SemanticManifest.h`
- Modify: `src/ui/SemanticManifest.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TestTargets.cmake`
- Create: `tests/headless/HeadlessWorkflowTests.cpp`

**Interfaces:**
- `HeadlessWorkflowRequest`: validated `SemanticManifestImport`, explicit accepted object indices, output directory.
- `HeadlessWorkflowReport`: backend/provenance, dimensions, accepted object names, event count, source hash, output paths, source-asset read count, and explicit non-claims.
- `runHeadlessWorkflow(const HeadlessWorkflowRequest&, std::stop_token)` returns `Result<HeadlessWorkflowReport>`.
- `SemanticManifestImport` carries the source `ProfileId` and exact ICC bytes obtained by the existing decoder.

- [ ] **Step 1: Write a failing integration test.** Build a 64×64 `fixture-no-ai` source image and two non-overlapping, full-coverage masks in test code. Call the wished-for headless workflow API with explicit accepted indices. Assert two separate objects, non-empty real `StrokeEvent` output, object-targeted layers, and no source-image payload in stroke events.
- [ ] **Step 2: Verify RED.** Compile/run the new test and confirm it fails because the headless API/target is not implemented, not because the fixture is malformed.
- [ ] **Step 3: Preserve source ICC provenance.** Extend `SemanticManifestImport` with decoded profile ID and bytes; populate them from the same `decodeImage` result used by manifest validation. Reject inconsistent profile ID/bytes.
- [ ] **Step 4: Implement the orchestration API.** Filter only explicitly accepted indices; pass accepted masks through `ReviewSession::create/accept`; create a C++ Document and object×paint-layer nodes; attach source/profile/mask resources; call `runReconstructionJob`; decode the delta; create and apply `reconstructionCommand` atomically.
- [ ] **Step 5: Verify GREEN.** Run the targeted integration test, then the complete available Linux CTest suite. The fixture report must remain `fixture-no-ai`, `wholeImageFidelityVerified=false`, and `naturalProcessReviewed=false`.
- [ ] **Step 6: Commit Task 2.**

### Task 3: Save, event replay, PNG frames, and correct ORA layers

**Files:**
- Modify: `src/headless/HeadlessWorkflow.cpp`
- Modify: `src/codecs/ExportService.cpp`
- Modify: `src/ui/PaintedDocument.cpp` only if effective ancestor visibility is not already available
- Modify: `tests/headless/HeadlessWorkflowTests.cpp`
- Modify: `tests/codecs/ExportMatrixTests.cpp` if the ORA behavior is better isolated there

**Interfaces:**
- Output files: `project.draw`, `zero.png`, `middle.png`, `final.png`, `painted.ora`, `report.json`.
- Frame limits use the existing `EventLimit`; no separate JavaScript/Python replay implementation.
- ORA contains editable leaf raster layers only; masks are applied to export-copy alpha, effective ancestor visibility is retained, object names are retained, and group boundary markers are omitted.

- [ ] **Step 1: Write failing export/replay assertions.** Assert that zero/middle/final PNGs have the requested dimensions and distinct expected frame hashes; ORA stack contains the two object-specific leaves in correct order, with hidden parent/masked pixels absent; `.draw` reopens and renders identically after the source image is moved away.
- [ ] **Step 2: Verify RED.** Run the new test against the existing export path and record the exact failing assertion(s), especially visibility/mask/order.
- [ ] **Step 3: Implement event-only frame export.** Render event-count limits through `renderPaintedDocument`, encode PNGs with the project ICC profile, save with `ProjectStore`, and export ORA through `ExportService`.
- [ ] **Step 4: Fix only the demonstrated ORA conversion defects.** Skip PSD group boundary records, preserve effective visibility and opacity, apply the effective mask to the exported pixel copy's alpha, and keep object identity in layer names. Do not expand PSD/PSB tag support.
- [ ] **Step 5: Verify GREEN.** Reopen `.draw` after moving the source image; assert identical final pixel hash and `sourceAssetReads == 0`. Run the full Linux CTest suite.
- [ ] **Step 6: Commit Task 3.**

### Task 4: Real manifest CLI, deterministic smoke fixture, and CI evidence

**Files:**
- Modify: `apps/headless/Main.cpp`
- Modify: `tests/headless/test_headless_cli.py`
- Modify: `.github/workflows/linux-headless-p0.yml`
- Create: `reports/linux-headless/P0_READINESS.md` (generated from actual run evidence; never hand-edit test counts)

**Interfaces:**
- `DrawboradHeadless run --manifest PATH --output DIR --accept-all` loads only the existing verified `sam3-image` manifest; acceptance is explicit.
- `DrawboradHeadless smoke --output DIR` uses a deterministic, visibly labelled `fixture-no-ai` input to validate the C++ engine without model weights.
- GitHub Actions builds on Ubuntu 24.04, executes the full CTest suite and smoke command, and uploads `.draw`, PNG, ORA, report, raw logs, and configuration receipt.

- [ ] **Step 1: Write failing CLI integration tests.** Test argument validation, explicit `--accept-all`, smoke artifact generation, and refusal to relabel a fixture as SAM inference.
- [ ] **Step 2: Verify RED.** Run the CLI tests before implementation and inspect exact failures.
- [ ] **Step 3: Implement manifest and smoke commands.** Reuse `loadSemanticMaskManifest` for real SAM3 results; construct the fixture explicitly in the smoke command and mark its provenance `fixture-no-ai`.
- [ ] **Step 4: Add bounded GitHub Actions verification.** One Linux job installs Qt6 Core/Gui and existing codec development libraries, configures/builds headless, runs all CTest entries, runs smoke, and uploads evidence. No GPU runner, paid resource, or model-weight download.
- [ ] **Step 5: Verify GREEN.** Inspect the exact Actions commit, every executed step, CTest inventory, uploaded artifact contents, and SHA-256. A green CPU fixture run is not a real SAM3 inference pass.
- [ ] **Step 6: Commit Task 4 and open a Draft PR against `work/cloud-agent-runtime-20261028`. Do not merge.**

## Completion Contract

P0 is complete only when the exact branch SHA has a successful Ubuntu 24.04 configure/build/CTest run, the smoke command produces all six artifacts, the `.draw` project reopens and replays after the source image is removed, ORA layers pass independent structure/pixel assertions, and the report keeps model/fidelity/natural-process claims honest. No Windows, GPU, Photoshop, or high-fidelity pass is implied.
