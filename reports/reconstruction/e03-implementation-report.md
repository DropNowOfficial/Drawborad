# E03 - Bounded local pigment refinement (2026-10-08)

Parent: `impl/reconstruction-e02` / `242a8d6f342dd2aaa81b1ffadaf4d1bd00895d08`

## Implemented

- `measureRegionProgress`: renders the selected reconstructed layer through R01 in <=512px tiles, comparing current pigment against color-managed source samples within the reviewed ROI. Separately reports native-space channel MAE, alpha MAE, alpha outside the reviewed mask, and pixels whose residual still exceeds the tolerance. A percentage or a count of emitted strokes is never a completion claim.
- `refineRegion`: ranks bounded per-grid local error hotspots and emits deterministic short native RGB/CMYK 8/16 bit `colored-pencil` events, with stable IDs, monotone times, finite event/sample budgets, and revision/identity validation. It returns `reconstruct.needs-review` when it cannot safely generate any stroke under the allowed budget, rather than fabricating image residuals.
- Conservative brush-footprint safety: all generated endpoints have fully accepted opaque review-mask neighborhoods, making transparent holes and adjacent objects protected even after reference rendering. Pixel-accurate fractional-mask edge refinement is **not** claimed. Such cases remain NeedsReview and require later mask/edit passes.
- The output EventBatch contains only serialized brush semantics and pigment, never the original image handle or source pixels. Source removal does not change the rendering of staged strokes.

## Fresh local evidence

- RED: 10 behavior tests failed on explicit stub behavior, not compilation or environment failures.
- GREEN: 10/10 pass; two additional tests for fractional-mask NeedsReview and cross-512-tile ROI also pass (12/12).
- Archived source full portable run **without** M01 patch: 35 CTest entries, 1 failed (`M01` malformed numeric JSON), 2 expected skips, E03 green. That archived defect was fixed previously in remote commit `985efc4a`.
- Archived source with *only that existing M01 fix temporarily overlaid for verification*: 35 CTest entries, 0 failures, 2 expected skips (`F05.NativeTransport` and `F06`); total ~26s. The temporary M01 patch was removed from the implementation worktree after verification.

**Evidence limitation:** The whole worktree is a historical archived source snapshot. It includes ABI-compatible local E01/E02 public contract headers for compiling E03, but the exact remote E02 full implementation was not rebuilt together with these tests. A remote branch push and file content matching does NOT replace exact-HEAD CI, Windows native testing, ORT model runs, or real Photoshop quality gates.

## Interface ruling and boundaries

- Ruling: `measureRegionProgress` returns `Result<QualityProgress>` instead of a bare struct to report invalid source/profile/mask, stale revision, cancellation, or rendering errors safely. Cost if wrong: downstream calls must unwrap a `Result`.
- This is a locally validated **refinement engine component**, not an E06 high-fidelity oracle. Native-channel MAE is not perceptual Delta E00, SSIM, or a substitute for the independently required three quality gates.
- E04 follows: independently reject bad critical lettering/logos, optional explicitly consented offline text candidates, no automatic font substitution or fake editable text claims.
