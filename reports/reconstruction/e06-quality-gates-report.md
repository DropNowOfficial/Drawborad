# E06 checkpoint - Independent three-gate quality evidence

Date: 2026-10-08
Parent: impl/reconstruction-e05 (d8767f27032a26d6cb524e3757caf04b9dd76366)

## Implemented in this checkpoint

- `QualityEvaluator::evaluate` treats Fidelity, Layer Integrity, and Natural Process as separate mandatory gates. No global score can compensate for failing critical lettering/Logo ROI, wrong layer count, invalid source-free replay, or unconvincing drawing process.
- Fidelity threshold checks accept only independently supplied full-resolution/profile-matched metrics: SSIM >= 0.98; mean Delta E00 <= 2; p95 Delta E00 <= 5; alpha MAE <= 2/255, plus every measured critical ROI accepted. Missing evidence stays Blocked; non-finite or contradictory metrics are input errors.
- Layer gate requires explicit Photoshop reopen verification, object isolation, source-free replay and actual editable brush strokes; mismatched layer counts fail independently.
- Naturalness requires time-ordered source-free replay and an explicit human process review. `acceptQuality` additionally requires an attended, approved human final review, even if the conditional three gate fields are all Passed.
- The no-argument CLI prints machine-readable per-gate JSON and returns 77 (Blocked) without real external measurements, rather than reporting a fake green status.

## Fresh tests and limitations

- TDD: 11 behavior cases had 10 expected stub failures before implementation. All 11 passed after implementing the gate logic.
- JSON CLI test was independently RED (plain text could not be parsed), then GREEN when three blocked states and `accepted:false` were emitted.
- Linux historical-source portable CTest (E04+E05+this E06 checkpoint) with the previously merged M01 numeric-grammar fix `985efc4a` temporarily overlaid only for verification: **38 CTest entries, 0 failures, 2 intended platform skips**. The local-only M01 overlay was removed after verification.

**Critical distinction:** This is only the evidence aggregation/decision gate for E06. It does NOT calculate real SSIM, ICC-converted Delta E00, prove Photoshop PSD interoperability, inspect a real image-to-model-to-stroke-to-PSD chain, or replace human naturalness review. The current entire M2 pipeline remains **Blocked pending real external oracles**, regardless of these synthetic logical tests. Do not mark E06 as product acceptance passed.

Rulings:
- E06 consumes actual metric/evidence records rather than fabricating image-level quality in this module; external instrumented QA remains required.
- No missing oracle can be silently treated as success; the no-evidence runner always reports three Blocked gates.
- C04/C05 PSD code remains frozen unless a concrete Photoshop oracle demonstrates a high-impact defect.

Next: build an instrumented source-to-PSD acceptance harness, run model/Photoshop/human oracles on target machine, and integrate the verified pipeline into Windows UI U01-U06.
