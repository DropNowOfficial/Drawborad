# Semantic layer correction — 2026-10-26

Baseline: `5b23ca684dc021ccf7a8a3e7dfc87e7c80e7a5c3`. This document concerns the **Python Linux laboratory probe only**. It is not a native Drawborad desktop or live-model acceptance report.

## Reproduced defect

The previous `tools/foundation_probe.py` uses three manually named GrabCut rectangles (spoon, cup, saucer) and assigns all remaining pixels to `background`. The cup mask captured mainly cup interior. Its red body and handle fell into the red saucer region; the spoon fragmented. The only user-facing preview was composited, obscuring whether image objects existed as independent layers. Therefore the earlier `4 layers` count established file structure only, **not semantic correctness**. 

## Corrected, bounded evidence

Use the project's *previously recorded* six visible-region masks (`tests/fixtures/coffee-reviewed-scene/mask0..5.json`) instead of re-running GrabCut. They have hash-verified content and are documented in `review.json`.

Group six visible regions into four independent masks:

| Layer | Recorded source regions | Pixels |
|---|---|---:|
| wood_table | 0 · table and backdrop (unassigned visible remainder) | 127,872 |
| red_saucer | 1 · visible saucer | 54,857 |
| cup | 2 · visible cup body/handle, 3 · inner cup, 4 · coffee surface | 50,676 |
| spoon | 5 · visible spoon fragments | 6,595 |

- Every pixel is assigned to exactly one mask. The visible-only RGBA cutouts stack back to the original RGB pixels byte-for-byte with a fully opaque result.
- Hiding `cup` affects only the 50,676 pixels in its mask; red saucer and other layers remain byte-identical.
- An OpenRaster `.ora` with four separate pixel layers is produced and its ZIP/PNG/XML structure is verified in Python. Actual Krita/GIMP UI import was not run here.
- The existing basic stroke planner can independently generate and replay brushstroke events for each region. That does not prove natural or high-fidelity painting.

## Tests

Run from repository root in a Python environment with NumPy and Pillow:

```sh
python -m unittest discover -s tests -p test_semantic_layers.py -v
```

The three E09 tests use recorded fixture bytes and deterministic synthetic RGB pixels to verify cup-versus-saucer membership, pixel-perfect re-composition, independent visibility, and round-trip correctness of the four-layer ORA payload. In the separate Linux test workspace the existing four foundation tests also passed (7/7 total).

## Limits and next gate

No model inference was performed in this correction. `review.json` explicitly declares `human_reviewed:false` and `segmentation_semantic_accuracy_verified:false`; reflected object boundaries still contain uncertain pixels. `wood_table` is **visible-only** (transparent beneath objects), not a completed table. Moving objects without holes requires a separate user-authorized occlusion/inpainting capability. The native Qt/C++ flow, real modern model inference, Photoshop interoperability, Windows build, and painting-quality gates remain unverified.

References: `src/ui/ReviewJob.cpp`, `tests/reconstruct/FullSceneWorkflowTests.cpp`, `tests/fixtures/coffee-reviewed-scene/review.json`, `tools/semantic_layers.py`, `tests/test_semantic_layers.py`.