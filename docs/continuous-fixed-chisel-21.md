# Native engine 21: continuous fixed chisel (bounded first version)

Engine21 / `chisel-ink-aa` is a new replay contract. Engines2,12,19 remain unchanged. This is a drawing-tool capability, not reference restoration, automatic drawing, recovered image detail, or artist-level artwork.

- Elliptical nib fixed at aspect3:1. `style.angle`0..7 means **k×22.5°** (0..157.5°), unlike engine2's45° steps. Index4 is90°. Width.5..128px, pressure0..1, opacity0..1; round hardness1, empty grain, seed0. Valid input tilt/device metadata is retained but does **not** rotate or deform this fixed nib.
- Exact-in-form segment membership test for a continuously swept homothetic ellipse with linearly varying pressure radius. Endpoints plus convex-quadratic stationary point are tested in nib coordinates. No finite dab spacing and no randomized texture.
- Pixel coverage is **4×4 fixed sub-sample union**, not exact pixel area. Subpixel thin marks can disappear at this sampling resolution. Union bits are combined before opacity blending; self-overlap within one stroke does not repeatedly apply opacity.
- Tile<=512×512; coordinates finite and bounded±1e9 in replay, helpers require within canvas; raw points<=4096/stroke, helper document<=8192events/1Mpoints. Coverage charges at most32Mi sub-sample evaluations per tile and checks cancellation each segment/scanline. Budget refusal is not silent downsampling. Very complex long strokes can exceed the budget. This is a per-tile/per-event evaluation cap, not a total-document CPU deadline; the existing merged-render work estimate is not an exact count of this algorithm's 16 sub-samples. Cancellation remains available and a failed full-render check does not commit the stroke.
- Partial-time replay uses the recorded timestamps; zero-pressure/zero-length cases are explicit. Geometric arithmetic uses fixed direction constants and floating arithmetic; Windows-specific numerical and input acceptance is not established by Linux tests alone.

## Storage and permissions of other tools

Format21 takes precedence over20; it always writes a bounded fill-operation-table count, including zero. Existing1..20 payloads remain unchanged. Old readers must reject21. Cross-generation save/journal protections require a new filename, not overwriting an older native project.

Engine21 is accepted as an independent native finishing mark and may remain after safe editing of an earlier engine19 stroke. It is **not** eligible for engine19 local point editing, explicit closure or closed-ink-fill ownership. No permissions are expanded by sharing a visual appearance.

## Tests and proof

B42 covers half-alpha direction widths/no periodic gaps at pressure.25/.5/1, angle4 axis swap, collinear subdivision, integer translation, quadrant stitching, partial replay, zero pressure, negative-origin coverage, tilt no-effect, opacity/self-overlap, malformed style/budget/cancel, old19/2/12 golden hashes, native serialization empty/full metadata tables and protected save. B41 remains passing. Full Qt, Windows and long-session tablet validation are separate checks.

Technical comparison output is maintained outside release sources in `chisel-production-proof`; it is diagnostic geometry, not a new illustration.

## Verified desktop checkpoint

Local Release suite: 108/108 passed (32.90s), including actual Linux Qt software/QML integration (20.34s). Tests exercise the real mode switch/angle selector, an uncommitted mouse preview, narrow/broad axis widths, synthetic tablet pressure and retained tilt, mode-change cancellation, invalid angle rejection, exclusive modes, Undo/redo, save/reopen pixel equality and refusal to overwrite an older-generation file. These are synthetic input packets, not physical pen or Windows acceptance.

A preserved real older-reader executable rejected the new native fixture with `project.engine-version`; the file SHA256 was unchanged before/after. Production-library diagnostic marks were visually inspected against old engines. No reference image, rejected drawing or diagnostic image is included in the published source changes.

Still incomplete: faithful finished artwork, real hand-drawn shape/composition judgment, physical tilt-driven nib deformation, pen-hair material dynamics, Windows/device validation and long-session usability. Stage updates distinguish validated tool capability from unfinished artwork.
