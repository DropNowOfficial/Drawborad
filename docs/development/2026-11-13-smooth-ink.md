# Opt-in broad antialiased ink (engine/format 19)

The private reference review found that global smoothing was the wrong next move: existing reconstruction curves were already overly geometric. A specific defect was reproducible instead: broad legacy marker tips formed dotted and stepped edges. This increment adds **Smooth solid ink**, an explicit new brush, not an automatic rewrite of old strokes.

- Reuses the continuous round-segment coverage kernel with pressure and 0.05–128 px width. New native events remain recolorable and undoable.
- Engine 10 fine pen keeps its 8 px limit; legacy marker geometry and replay are unchanged.
- New engine 19 uses `smooth-ink-aa`; format 19 prevents old-reader fallback. New copy is required when crossing a save-format boundary. The unintegrated contour prototype format18 is not silently claimed supported.
- Original coordinates, width and pressure are not inferred from an image. No generative model or image-upscaling algorithm is involved.

Local verification: 88/88 CTest and 70/70 real-native Python tests (23 subtests). Focused tests additionally cover 128 px tile seam, zero pressure, a single-point canvas edge and mismatched engine/tool. An actual pre19 `3528e1b` reader rejects a format19 file even with an old `.bak` present; both files remain byte-identical. Legacy private eye-study output rendered through the new build matches the old pixels exactly. Save-copy, undo/redo and reopen pass for new ink.

Independent visual review of the same hand-authored eye study: broad line tips are visibly cleaner. But left hair still has stepped scanline-fill edges, right hair looks like ribbons, eye/pupil/hatching remain overly regular, and source brush variation/composition are not restored. This is a useful edge-rendering repair, **not finished artwork and not a general artistic-quality breakthrough**. Private references, authored study coordinates and outputs are excluded from GitHub.

New UI handler tests are VM/DOM tests, not actual browser/Windows acceptance. Remote CI needs verification on the exact uploaded head.
