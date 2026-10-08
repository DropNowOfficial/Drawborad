# E01 — Reconstruction Event Provenance: implementation evidence

Date: 2026-10-08  
Branch: `impl/reconstruction-e01`  
Base: `ca0976ad9fa8a47aefba2d2029372c3159dbc95a`

## Scope and ruling

This is **E01 only**, the first task of the M2 reconstruction vertical slice.
The E01 compiler receives source pixels as a temporary read-only capability,
ReviewedObjects, and already proposed stroke events. It validates and stages
those events; it does **not** invent AI-produced contours or pigment plans.
Those algorithms belong to E02 and E03. The returned EventBatch has no
bitmap, pointer, source-asset handle, or source-image fallback.

The compiler verifies the review revision and object/layer binding, checks
sample centres against the reviewed mask, enforces event/sample/width budgets,
blocks embedded bitmap or residual-style event types, and enforces temporal
ordering and native document color/profile compatibility. Stage output can be
published only through a single version-checked Document::apply transaction.
Cancellation and stale revision reject without partial publication.

## Tests and exact environment qualification

- E01 RED: the initial eight behavioral cases failed (6 assertions failed)
  against stub compilation/validation/commit functions; two negative cases
  already returned an unsupported error. Two additional tests reproduced
  missing nested-group and temporal-order behavior as RED.
- E01 GREEN: 10/10 behavior cases, 0 failures; CTest `E01` 1/1 passed.
- Archived source snapshot `65b9c0be` plus **only a local test-harness
  overlay** of upstream fix `985efc4a` (M01 JSON grammar), full portable
  CTest: 35 entries, 0 failed, 2 designed skips (F05.NativeTransport/F06).
  The unmodified archived snapshot instead reports 1 known M01 failure.
- Six changed E01 file blob SHA values match the files compiled and tested
  locally exactly: `CMakeLists.txt`, `cmake/TestTargets.cmake`,
  `ReconstructionService.h/.cpp`, `EventWriter.cpp`,
  `ProvenanceTests.cpp`.
  The five existing build-contract/dependency headers examined also matched
  the archived source blob SHAs.

The archived snapshot test suite is **qualified local evidence**, not a
test of the exact whole remote branch. No successful Windows or GitHub CI
run has been claimed. Existing GitHub-hosted runner provisioning failures
and physical Windows/Photoshop/model qualification remain separately blocked.

## Known boundaries and next task

- E01 verifies centre-of-stroke mask membership. E02/E03 must constrain the
  actual painted footprint to the accepted object mask; E01 alone does not
  claim zero pigment leakage across edges.
- No reconstruction model inference, automatic stroke planning, style output,
  final fidelity score, or complete input-to-PSD pipeline was implemented here.
- E02 is next: coherent contour and region pigment planning, deterministic
  seeded scheduling, and independently verified local mask coverage. Stop
  after a bounded E02 task; do not reopen frozen C04/C05 by default.
