# Windows Integrated Alpha RC1: engineering handoff status

**Runtime Alpha: Blocked. No Windows application ZIP has been produced.**

**High-Fidelity Reconstruction: Failed / NeedsReview.** Historical SSIM 0.6487
is below 0.98. The current candidate has no new real-image quality measurement.

This branch continues the existing full-scene code at
`69504ea037bf3a30c7a7afae2f19dc8742791610`; it does not replace the application.
The current executor is Ubuntu 24.04 x86_64 with GCC 13.3, not Windows 11/MSVC.
The user's prior Windows build/model directories are not accessible here.
No main merge, force push, old-branch deletion, hosted Actions run, paid resource,
billing change, self-hosted runner installation or model-weight upload occurred.

## Evidence matrix

The status vocabulary below is Implemented / Tested / Passed / Failed /
Blocked / Not Tested. Final machine-readable complete-suite receipts are
generated after the candidate commit is fixed; they carry the exact source SHA,
source fingerprint, effective CMake cache, input hashes, test inventory, JUnit
hash and execution times. Earlier working-tree logs are labelled separately.

| Gate | Status | Evidence or remaining requirement |
|---|---|---|
| Exact full-scene baseline and isolated release branch | Passed | All 299 blobs and Git tree matched the remote baseline |
| ONNX SDK parameter, CPU capability and deployment checks | Implemented | Explicit root, headers/import library/DLL, x64 checks, same SDK deployment, licenses |
| Seven-state model preflight | Tested | CPU runtime synthetic encoder/decoder execution plus failure cases; not actual SAM |
| Model provenance and hash-bound UI receipt | Implemented | Manifest/encoder/decoder hashes, source/license/version/adapter metadata retained |
| Review atomicity and stale revision protection | Implemented | Result installation checks identity/revision and stages the verified source first |
| Zoom/pan, cancellation, EXIF and UI regression coverage | Implemented | Native Qt Test additions exist; native execution is Blocked |
| Portable C++/Python regression tests | Tested | Independent Linux runs; final receipts identify the precise committed snapshot |
| PowerShell syntax | Passed | Actual PowerShell Parser.ParseFile AST check; not native build execution |
| Strict test registration/evidence gates | Tested | Missing integrations, changed source/config/input, skips and stale receipts rejected |
| Exact coffee original | Passed | 600 x 400, SHA-256 cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7 |
| A: Windows x64 Release application and Worker build | Blocked | No Windows/MSVC/Qt executor |
| B: Start from a clean extracted Windows ZIP | Blocked | No Windows ZIP or native runtime |
| C: Actual Windows drawing/save/open/layers/replay/cancel | Blocked | Qt Test source is not native runtime evidence |
| D: Real SAM 2.1 CPU coffee workflow | Blocked | No local SAM weights or official reference tensor set; no Windows UI |
| E: Real workflow saved-stroke/source independence | Blocked | Core persistence/replay checks do not replace attended real-image acceptance |
| F: Windows portable ZIP, checksum and extracted smoke | Blocked | Packaging recipe exists; executable artifact does not |
| G: Separate runtime, fidelity and Photoshop reporting | Implemented | No automated suite success promotes these independent gates |
| Actual coffee PSD independent structure/composite check | Blocked | No current real-workflow export; core PSD tests are a different scope |
| Photoshop Oracle | Blocked | Not Run; actual Photoshop unavailable |
| Current SSIM, color/alpha error and text/logo ROI | Not Tested | No new real-workflow measurement; threshold unchanged |
| Final high-fidelity acceptance | Failed | Historical 0.6487 does not meet >= 0.98 |
| Human natural-painting-process acceptance | Not Tested | Human review still required |
| RTX 4070 CUDA | Not Tested | Deferred until complete CPU acceptance |

All 17 attended coffee-workflow steps in the Windows protocol remain Blocked
in this session. Recovering the correct source image and exercising tiny ONNX
graphs do not advance those steps to Passed. There is no current Windows UI
screenshot, zero/coarse/refined coffee output, real-coffee stroke count, or
Photoshop screenshot to present.

## Reproduce and continue on Windows

Use [the exact Windows protocol](../../docs/alpha/WINDOWS_ALPHA_TEST.md) with a
clean checkout of the candidate's full SHA and verified existing inputs. It
contains both strict Integrated-CPU and explicit UIOnly commands, the 17-step
workflow, native UI checks and independent PSD/Photoshop/fidelity requirements.
Use [the package guide](../../tools/windows/PACKAGE_README.md) for SDK notices
and diagnostics. A model-less package must keep its UIOnly name.

The builder retains caches/logs, records every registered and unregistered test,
audits official windeployqt dry-run output before deployment, then tests both a
candidate extraction and the final immutable ZIP extraction. The final checksum
is written only after that final smoke succeeds. No guessed ZIP path or hash is
an application download.

The hosted workflow is manual-only and limited to an explicitly unqualified
UI-only candidate. Missing preinstalled native prerequisites are blockers, not
a reason for unbounded SDK builds or repeated hosted dispatches.

See also [input provenance](INPUT_PROVENANCE.md),
[source review](REVIEW_NOTES.md), and
[focused model evidence](model-test-evidence.md). The historical 49/49 Debug
claim lacks matching logs/cache/binaries in this environment and remains
unverified as a current release qualification.
