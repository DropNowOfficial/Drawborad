# ARCHIVED: semantic object separation failed (2026-10-26)

> **Do not treat this 4-mask GrabCut probe as correct semantic separation.** The red cup body and handle were mistakenly included in the saucer layer, despite 4 disjoint mask files and source-free replay. See [Semantic layer correction](SEMANTIC_LAYER_CORRECTION_20261026.md) for the independent wood/saucer/cup/spoon regrouping of six recorded scene masks and regression tests. The correction still does not prove live model recognition, inpainting or painting quality.

---

# Linux foundation probe: segmentation -> layers -> brushstroke replay

**Scope:** isolated executable laboratory probe, **not** the native Drawborad editor, SAM 2.1/3.x, a semantic recognition model, or shipping application code.

This bounded experiment tests a more fundamental prerequisite than optional stroke editing: can a real image produce independent object masks, stored per-object brushstroke events, and reproducible replay without an original bitmap? It deliberately reuses existing Qt/C++ Drawborad concepts while keeping experimental code separate. No shipped rendering, model adapter or PSD code is replaced.

## Run

Requires Python 3, NumPy, Pillow, opencv-python, and scikit-image **already installed**. Nothing downloads weights or images.

```sh
python3 -m unittest discover -s tests -p test_foundation.py -v
python3 -m tools.foundation_probe \
  --input /path/to/coffee.png --out /path/to/lab-output \
  --rect spoon:310,42,135,290 \
  --rect cup:163,11,267,287 \
  --rect saucer:74,75,413,322
```

Rectangles and the names are **human-provided prompts**; the probe uses classical OpenCV GrabCut, **not SAM, not automatic semantic naming**. Frontmost prompted objects have priority; the remainder is explicitly named `background`.

Outputs: `project/manifest.json`, hash-checked `project/mask-*.u8`, editable `project/events.json`, `previews/layer-*.png`, `previews/mask-overlay.png`, and `previews/replay-{000,025,050,075,100}.png`. Replay renderer only accepts event and mask data, never source image bytes.

## Verified local Linux sample (2026-10-26)

Input is the repository's exact 600 x 400 scikit-image coffee fixture, SHA-256 `cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7`.

- 4 independent partition masks including background; 240,000/240,000 pixels assigned once.
- 8,333 stored brushstroke events across base and detail phases.
- 0 source asset reads during event-only replay.
- 4/4 focused Python behavior tests passed, including source-file removal, isolated region recoloring, and rejection of incomplete/overlapping partitions.
- **Observed RGB SSIM = 0.6249846601; MAE = 0.0362970915** (RGB/255).
- **Fidelity: Failed. Natural process: not qualified. SAM inference: not run. Native Windows/Qt: not run.**

These numbers are the **lab probe's**, not the RC1 C++ native engine's or the earlier historical SSIM=0.6487. Do not compare them as an algorithm improvement.

## Next single integration gate

Connect this same real-image input and reviewed masks to the **existing native** `runReconstructionJob` / `renderPaintedDocument` path. That requires a Linux Qt 6 toolchain and a synchronized native checkout. Only after the real native painting result is measured should the actual supported model package be qualified and substituted for these prompted masks. Current container has no Qt 6 or official model weights, so that gate remains **Blocked**.

Project standard remains: replaceable modern model backends, high-quality drawing, Apple-caliber UI/UX, layered PSD and Windows production quality. No acceptance criterion has been lowered.

