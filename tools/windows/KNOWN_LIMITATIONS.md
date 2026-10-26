# RC1 acceptance limits

- This candidate is unsigned. Signing, installer certification, tablet behavior,
  long sessions and real Windows 11 user acceptance require separate evidence.
- An automated suite result of `Passed` is not Runtime Alpha acceptance. The
  required real model workflow, full-scene review, cancellation/resource behavior,
  export/reopen checks and manual application evidence must be reviewed together.
- CPU ONNX Runtime is the only integrated runtime covered by this recipe. CUDA,
  DirectML and other accelerator providers are not packaged or qualified here.
- No model weights are bundled or downloaded. Missing, altered or incompatible
  model packages must remain visibly unavailable; UI-only packages cannot infer.
- A DLL import audit covers PE imports, including imports reported by DUMPBIN.
  Libraries selected dynamically at runtime require actual application exercise.
  The extracted-ZIP startup checks cover Qt/QML startup and worker transport,
  not every codec, model operation or export path.
- Hosted Windows Server 2022 UI-only builds are not Windows 11 qualification.
  Native Windows execution must be recorded; Linux/source checks are not a
  substitute. See the actual statuses and blockers in `TEST_RESULTS.json`.
- The historical coffee full-scene score was about 0.6487. It is below the
  required acceptance target and must not be described as passing. This packaging
  work does not improve or requalify that score.
- Preserve original inputs and use test copies. Source-image embedding, editable
  layered exports and Photoshop interoperability must only be claimed where the
  corresponding real workflow and reopen oracle have been exercised successfully.
