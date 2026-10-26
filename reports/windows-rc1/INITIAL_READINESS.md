# Initial Release Readiness Matrix

Inspection date: 2026-10-26. Source SHA: `69504ea037bf3a30c7a7afae2f19dc8742791610`; tree: `2027d1ceea8a62b4b42abd20ae6f1bb453caa436`. Every one of 299 fetched blobs and the full reconstructed Git tree matched GitHub. New candidate branch: `release/win11-integrated-alpha-rc1`.

Only the status vocabulary Implemented / Tested / Passed / Failed / Blocked / Not Tested is used. This is the initial snapshot, not the final candidate result.

| Item | Status | Evidence / reason |
|---|---|---|
| Baseline and isolated branch | Passed | Remote ref and exact local Git object/tree verified; no main update |
| Drawing/review/reconstruction/refinement/replay/.draw/PSD paths | Implemented | Existing requested C++/QML files and test source audited |
| Current portable build/tests | Not Tested | Independent toolchain setup in progress |
| Windows x64 Release compile | Blocked | Linux executor; no MSVC or Qt 6.12 |
| Windows UI and Qt Test run | Blocked | No native Windows execution surface |
| ONNX Runtime packaging integration | Failed | Build-Alpha did not accept/pass SDK root |
| Seven-state model readiness | Failed | No unified preflight; static compatibility can be supported without inference |
| Conditional test completeness | Failed | Missing dependencies silently remove integration registrations |
| Real coffee source and SAM CPU inference | Blocked | Masks/review JSON present; original image, weights and official references absent |
| Extracted ZIP smoke design | Failed | Old script tested stage and diagnostics exited before Qt initialization |
| Current PSD independent structure | Not Tested | Historical assertions do not establish current output |
| Photoshop Oracle | Blocked | Not Run; no Photoshop |
| Current fidelity measurement | Not Tested | No current source/model/reference inputs; historical SSIM 0.6487 remains below 0.98 |
| Historical 49/49 claim source/log match | Blocked | Commit message contains claim; matching 2026-10-24 full log/cache/binaries not included |

The commit's historical Debug 49/49 claim is not entered as a current Passed result. No current Windows screenshot or application ZIP exists. An initial local CMake invocation failed because the temporary Ninja path was incorrect; the corrected invocation uses `/tmp/drawborad-rc1-build-tools/bin/ninja`. This is setup evidence, not a source compile failure.
