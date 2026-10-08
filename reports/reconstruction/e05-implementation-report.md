# E05 - Versioned reconstruction styles and source-independent variants

Date: 2026-10-08
Parent: impl/reconstruction-e04 (049368e5255d0483077cbc40515b6c086af12469)

## Implemented

- Five strict offline JSON presets (`fidelity`, `dry-media`, `marker`, `pencil`, `concept-sketch`) with schema version, allow-listed tool, width/opacity scaling, nib hardness, grain and pressure variation. Unknown IDs and path traversal are rejected; malformed, duplicate, missing or unsafe preset values fail explicit validation.
- `StyleRegistry::load` loads only allow-listed preset files. `makeStyleVariant` uses deterministic provenance hashes and per-event integer seeds to apply real stroke-media transformations to existing editable events, not raster filters or bitmap replacements. Every non-Fidelity variant returns a visibly different brush rendering in tests.
- Fidelity never modifies brush semantics, pressure, tools or existing RNG seed. Other variants cannot widen a pre-existing stroke and thus cannot introduce additional mask-edge spill solely by increasing brush size. Wrong tools and incompatible provenance-resource collisions are rejected.
- Original object/layer identifiers, color profiles, source pigment values, and baseline snapshot are preserved. Each variant has a separate document ID and SHA-256-addressed Reference resource encoding style identity, version, seed and preset digest; project save/recover and replay have been tested without models or source pixels.

## Fresh test evidence

- RED: 9 baseline style cases produced 7 assertion failures against the explicit stub; 2 invalid-input negative tests already passed.
- GREEN: 9/9. Two stroke-width/seed regressions reproduced 2 failures and passed after fixing, then two forged-tool/provenance-collision regressions reproduced 2 failures and passed.
- E05 final targeted: **13/13 behavior cases, zero failures**.
- Complete portable CTest against archived source with already merged upstream M01 fix `985efc4a` temporarily overlaid only in the local validation copy: **36 CTest entries, zero failed, two designed skips**. The verification-only M01 patch was restored afterward.

This is qualified historical-source and native reference-renderer evidence; it does **not** prove that the complete exact E03/E04/E05 GitHub branch builds on Windows or passes current GitHub Actions, genuine Photoshop reopen/save/reopen, model reconstruction or human naturalness review.

## Deliberate boundaries and rulings

- The E05 public API returns richer `ReconstructionStyle` metadata rather than E02's four-field `StyleSettings`, because version, pressure/grain and persistence provenance would otherwise be lost. Costs: a later adapter is needed if calling E02 `planBasePigment` directly with an E05 manifest.
- E05 transforms stored events with a real brush renderer and persists source-independent variants. It is **not** proof of five separate fully trained reconstruction algorithms or the fidelity of any one image against a real-world reference. This requires E06 metrics/oracles.
- E06 remains the three-gate, exact-quality acceptance stage. Photoshop, target GPU hardware and human quality assessments are not substituted with local synthetic tests.
