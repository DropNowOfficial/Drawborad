# E02 — Coherent contour/base stroke planning and deterministic scheduling

Date: 2026-10-08
Parent E01 remote ref: `1e316489b5c5b969c2b610e77c2e19ae78bea555`

## Implemented scope

- `planContours`: deterministic clockwise-directed raster boundary tracing of a
  conservatively eroded accepted review mask, including inner holes, with a
  continuous closed `guide` event per contour. No source texture or bitmap
  attachment is used, and source byte removal does not change the planned
  contour events.
- `planBasePigment`: long, bounded, single-color pigment strokes over the
  accepted interior, with RGB/CMYK U8/U16 color samples read in native profile.
  Coarse stroke orientation follows a dominant isophote direction of the source
  region. No per-pixel event substitution or whole-image residual events.
- `scheduleStrokes`: deterministic region grouping, contour-before-base ordering,
  guaranteed unique IDs and monotone event timestamps. A stroke leaving the
  declared region is rejected, not silently assigned based on its first point.
- Integrates the resulting EventBatch with E01 compile/atomic commit and R01
  source-free reference replay, including a rendered transparent-hole oracle.

## Validation evidence (source provenance matters)

RED: first 9 behavioral tests failed against intentional not-implemented stubs.
GREEN: 9/9 after implementations. Two bounded Important regression tests were
RED→GREEN, covering cross-region paths and budget-adaptive sampling (11/11).

Portable CTest from the archived 65b9c0be source snapshot, E01 feature, E02
feature, and the previously shipped M01 numeric JSON fix `985efc4a` temporarily
applied to **the local build only**: 36 entries, zero failures, 2 expected skips
(`F05.NativeTransport`, `F06`). Unpatched archived source retains its known
M01 JSON failure. The temporary M01 file change was restored after testing.
The E02 test suite has 11 behavior cases and 0 failures.

**This is qualified local verification, not exact remote branch CI.** Windows,
GitHub-hosted checks, actual Photoshop reopen/save/reopen, and model quality
remain separately unverified/blocked; no claims of end-to-end M2 completion.

## Deferred to E03 and further gates

- The 255-only safe core intentionally excludes feathered/partially accepted
  mask boundaries; E03 must handle edge coverage and fidelity without leakage.
- Global per-region gradient tangents are coarse planning only. E03 must handle
  multiple texture/fold orientations and quality feedback.
- Current work does not claim complete source-image fidelity, full style set,
  dynamic model-generated strokes, or Windows/Photoshop acceptance.

Next bounded task: E03 locally budgeted error-driven pigment refinement. Do not
reopen frozen C04/C05 without a real external oracle regression.
