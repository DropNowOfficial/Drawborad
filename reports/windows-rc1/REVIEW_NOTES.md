# RC1 bounded integration review

A fresh read-only reviewer inspected the complete working diff against
`69504ea037bf3a30c7a7afae2f19dc8742791610`, including untracked new code and the
implementation plan. This is source-review evidence; it does not qualify a
Windows build. A separate earlier reviewer inspected the model CPU subsystem.

| Finding | Correction | Verification boundary |
|---|---|---|
| U01/U04 Qt Test executables searched beside themselves for a Worker that was built in another directory | Both UI test executables now use the same configuration output directory as the Worker; target-aware CTest commands remain | Source reviewed; Windows execution Blocked |
| Codec process test recursively selected the first executable from a reused multi-configuration build | CTest passes `$<TARGET_FILE:DrawboradCodec>` and the Python test consumes that exact file | Real Linux codec/BMP RED exit 1 before correction and GREEN exit 0 afterward |
| An old successful test receipt could be presented with changed CMake flags | Complete effective CMake cache and canonical inventory digest are bound to the execution receipt | Reproduced false green; regression tests and independent valid-reconfiguration check now return Blocked |
| Null or Boolean execution status was accepted as a zero exit | Require an actual integer exit status | Null, false, string `0` and nonzero integer are rejected |

The reviewer found no remaining Critical or Important source defect in that
bounded pass after these corrections. The reconfiguration regression was also
strengthened to use a valid newly computed inventory digest with an old receipt,
rather than testing only a corrupted inventory.

Scope rulings:

- Native MSVC/Qt/Windows UI, SDK deployment and extracted application execution
  remain Blocked. PowerShell AST parsing is a separate Linux syntax check.
- The exact coffee original has been recovered and its hash verified, but actual
  SAM 2.1 weights and official reference tensors are unavailable. Synthetic ONNX
  tests do not satisfy the real-image workflow.
- CUDA exclusive-GPU placement and CPU fallback behavior remain deferred until
  CPU acceptance. No CUDA qualification is inferred.
- No Photoshop or current high-fidelity acceptance occurred. C04/C05 scope and
  the SSIM >= 0.98 target remain unchanged.
- Hosted CI is manual-only; no workflow was dispatched during this session.
  A source review cannot establish hosted SDK availability or a successful run.

Raw regression logs from this session are under the ignored
`out/rc1-evidence` directory. Final complete-suite results must identify the
exact committed SHA and configuration in their own inventory/receipt; the
earlier focused model report explicitly identifies its working-tree scope.
