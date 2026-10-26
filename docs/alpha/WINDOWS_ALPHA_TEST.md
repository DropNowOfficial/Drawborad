# Windows Integrated Alpha RC1: build and acceptance protocol

Candidate branch: `release/win11-integrated-alpha-rc1`. Its starting point is
`impl/full-scene-checkpoint-20261025` at
`69504ea037bf3a30c7a7afae2f19dc8742791610`. Use the exact candidate commit from
GitHub, not the October 8 preview branch. This document is a procedure, not
evidence that Windows acceptance has run.

The current engineering session runs on Linux. No Windows executable ZIP,
Windows UI screenshot, actual SAM 2.1 coffee result or Photoshop oracle has
been produced in that session. Historical Debug 49/49 and SSIM 0.6487 do not
qualify this candidate. The fidelity target remains SSIM >= 0.98.

## 1. Reuse a verified Windows development environment

Use Windows 11 x64, Visual Studio 2022 with the x64 C++ workload, CMake >= 3.25,
Python >= 3.9 with PyYAML, Qt 6.12.x `msvc2022_64` including Qt Test, and the
official Microsoft Visual C++ x64 Redistributable. Preserve existing work and
build caches. Check that the source checkout is clean and its complete SHA
matches the intended candidate. Use a separate checkout/worktree if needed;
never reset or clean an existing user project to follow this procedure.

The local build script reuses a compatible cache and rejects an incompatible
one. Supply a separate `-BuildDirectory` to retain an older cache. Native
PNG/JPEG/TIFF/WebP/color dependencies must already be available. An installed
vcpkg tree can be selected with both `-CMakeToolchainFile` and
`-VcpkgInstalledDir`; the target triplet is `x64-windows`. The script copies
their actual copyright notices. It does not acquire model weights.

## 2. Integrated CPU build

Replace the example SDK/model/reference paths with verified existing inputs.
`-CoffeeReference`, `-CoffeeAutoReference` and `-CoffeeFullSceneReference` are
directories, not JSON filenames. The full-scene directory needs its original
`../reviewed-scene/review.json` and corresponding mask files.

```powershell
$candidateSha = (git rev-parse HEAD).Trim()
git status --short
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\Build-Alpha.ps1 `
  -ExpectedCommit $candidateSha `
  -QtRoot 'D:\Qt\6.12.0\msvc2022_64' `
  -QtLicensesRoot 'D:\Qt\Licenses' `
  -OnnxRuntimeRoot 'D:\Dependencies\onnxruntime-win-x64-1.23.2' `
  -CoffeeImage 'D:\DrawboradInputs\coffee.png' `
  -CoffeePackage 'D:\DrawboradModels\sam2.1-package' `
  -CoffeeReference 'D:\DrawboradInputs\coffee-native' `
  -CoffeeAutoReference 'D:\DrawboradInputs\coffee-automatic' `
  -CoffeeFullSceneReference 'D:\DrawboradInputs\full-scene-reference' `
  -CMakeToolchainFile 'D:\vcpkg\scripts\buildsystems\vcpkg.cmake' `
  -VcpkgInstalledDir 'D:\vcpkg\installed'
```

The expected coffee PNG is exactly 600 x 400, SHA-256
`cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7`.
Model contents are local `manifest.json`, `encoder.onnx` and `decoder.onnx`.
Retain original source/version/license/provenance and verify their hashes.
`Ready` requires a real deterministic encoder+decoder smoke invocation; it
does not assert coffee segmentation quality. The separate real-image suite
must also pass.

This command uses `RealWindowsIntegration`, builds all Release targets, runs
the complete registered CTest suite, verifies a Qt deployment dry-run and DLL
imports, deploys, creates a ZIP, and tests a fresh extraction with a sanitized
PATH. Missing dependencies or skipped required tests prevent a release ZIP.
`BUILD_INFO.json` records exact source/toolchain/architecture/capabilities.
`TEST_RESULTS.json`, receipts, inventory and raw logs identify that one run.

On success the script prints `ALPHA_ZIP`, `ALPHA_SHA256`,
`ALPHA_CHECKSUM_FILE` and `ALPHA_EVIDENCE`. The full source SHA is part of the
ZIP name under `out\alpha`. Verify the adjacent `.smoke.json` and `.sha256.txt`.
There is no download until those files actually exist and the final extracted
smoke check has passed. Startup checks alone leave Runtime Alpha acceptance
Blocked pending the attended workflow below.

## 3. Explicit UI-only alternative

When a model package/reference set is unavailable, use a separate build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\Build-Alpha.ps1 `
  -UiOnly -ExpectedCommit (git rev-parse HEAD).Trim() `
  -QtRoot 'D:\Qt\6.12.0\msvc2022_64' -QtLicensesRoot 'D:\Qt\Licenses' `
  -CMakeToolchainFile 'D:\vcpkg\scripts\buildsystems\vcpkg.cmake' `
  -VcpkgInstalledDir 'D:\vcpkg\installed'
```

This omits ORT and must be labelled UIOnly. It is not AI-ready. The manual
GitHub workflow is similarly limited to a UI-only Windows Server candidate,
requires the exact candidate branch/SHA and is not Windows 11 qualification.
There is no push trigger. Do not repeatedly dispatch hosted builds when runner
allocation fails. `steps=[]` is no evidence of a C++ compile failure. No billing
change or self-hosted runner installation is part of this procedure.

## 4. Attended acceptance from the extracted ZIP

Run as a normal user in a fresh writable extraction, with source/build paths
removed from PATH. Record the ZIP hash, Git SHA, Windows build, runtime version,
model manifest/weight hashes, input hash, each step's timestamps, screenshot,
and any precise error code/reproduction. Do not overwrite an original project.

| Step | Required evidence |
|---|---|
| 1 | Launch the actual desktop app; no missing Qt, MSVC or ORT dependency |
| 2 | Select the offline SAM 2.1 package; show the seven-state preflight result |
| 3 | Import the exact coffee PNG; correct size and orientation |
| 4 | Execute actual CPU automatic segmentation; retain worker log/provenance |
| 5 | Multiple candidates and finite confidence values displayed |
| 6 | Select, rename, merge and subtract overlapping regions; inspect masks |
| 7 | Correct masks with include/exclude points and a box; retain prompt audit |
| 8 | Inspect unassigned pixels and explicitly confirm needed background |
| 9 | Accept named objects and masks; no automatic acceptance implied |
| 10 | Generate real Guides / Base Pigment / Detail strokes; save zero/coarse/final frames |
| 11 | Perform one local refinement; save before/after and affected ROI |
| 12 | Verify other objects/layers and unrelated pixels unchanged |
| 13 | Replay zero/intermediate/final frames and retain event counts/statistics |
| 14 | Save `.draw`, close the process and reopen the saved project |
| 15 | Disconnect the source image; replay saved strokes independently |
| 16 | Attempt a wrong-hash source association; verify rejection and unchanged project |
| 17 | Export PSD and independently verify structure, masks and composite |

Also exercise New/Open/Save, real mouse strokes, zoom/pan/Fit, layer selection,
visibility/locking/order, review/draw transitions, acceptance/cancellation,
worker cancellation and responsive UI during a long job. Test source images
with EXIF rotation and compare displayed reference, mask and pointer coordinates.
Capture stale-result and failure behavior without losing the current document.

## 5. Independent quality and interoperability gates

PSD structure requires both project checks and independent `psd-tools` evidence:
readability, layer order/names, balanced groups, masks and matching composite.
Photoshop requires the actual application: Open -> inspect layers/masks -> Save
-> Reopen -> compare. If absent, record **Photoshop Oracle: Blocked — Not Run**.
Do not expand frozen C04/C05 scope to infer compatibility from unknown tags.

Retain original, review masks, zero-stroke, coarse/refined outputs, full-image
comparison, text/logo ROIs, SSIM/color/alpha errors, stroke/replay statistics and
sourceAssetReads. No original-image fallback or residual-image painting is
permitted. SSIM 0.6487 is historical failed quality evidence, not a current
measurement. **Runtime Alpha** and **High-Fidelity Reconstruction** must be
reported separately; runtime acceptance cannot imply SSIM >= 0.98.

Run `Run-Diagnostics.cmd` from the package and retain its generated diagnostics.
Consult [the package guide](../../tools/windows/PACKAGE_README.md) and
[known limitations](../../tools/windows/KNOWN_LIMITATIONS.md).
