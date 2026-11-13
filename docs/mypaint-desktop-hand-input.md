# Experimental desktop MyPaint hand input

Status: work in progress. This adapter is not a completed painting application or evidence of professional artwork quality. GitHub CI remains unqualified while the account billing limit blocks jobs. Windows and physical tablet use have not been validated.

## Purpose

Use actual hand-gesture pressure and elapsed timestamps with the already available, pinned MyPaint presets. Reference images are comparison only, never copied into the resulting stroke. The first implementation produces the precise engine result after pen release; a gesture guide must not be described as a live MyPaint rendering.

`pointy_ink` provides pressure-dependent width; `dry_brush` provides broken/particle ink coverage. The existing worker renders black grayscale coverage then applies a constant RGB tint. It is not pigment mixing, smudge, background sampling or tilt-aware painting. A radius multiplier adjusts the preset, not an exact pixel width. Mouse input uses explicitly simulated constant pressure, not measured pressure.

## Build/runtime boundary

`DRAWBORAD_MYPAINT_DESKTOP` is OFF by default and independent of the Qt solid-mask option. Portable core tools do not acquire a Qt dependency when this option is OFF.

With the option ON, the operator supplies existing local paths via the CMake cache:

- `DRAWBORAD_MYPAINT_PYTHON`: existing Linux Python with the required PNG dependency; development default `/usr/bin/python3`.
- `DRAWBORAD_MYPAINT_SOURCE_DIR`: source root containing `cloud/mypaint_worker.py` and its reviewed helpers/preset data; default is the build's source root.

These are trusted operator configuration, not fields accepted from documents. No packages, runtime libraries or models are installed automatically. The adapter verifies existing runtime/preset pins and rejects absent or changed runtime components. Source-path defaults are development configuration, not a self-contained release package.

The existing library notice is in `cloud/mypaint_data/libmypaint-COPYING`; selected preset notices are documented by the accompanying license files. Do not claim all upstream presets share one license.

## Candidate and preservation model

The child process is bounded and its output is validated before a candidate command is produced. A candidate does not itself modify the document. The desktop must compare document identity, revision and gesture generation again before acceptance. PNG/manifest hashes are integrity checks for trusted execution, not independent proof that arbitrary scripts are safe.

Accepted strokes are independent Normal raster layers inside a neutral top-level MyPaint group. They retain transparent coverage rather than baking a reference/background. Their rendered pixels persist using the existing cropped-raster format; reopening does not require running MyPaint. Undo returns the document to the previous state. Baked strokes are not editable vector control points.

## Explicit first-stage limits

- Native canvas: up to 8192 on either axis and 20 million pixels; no silent resize.
- Per-stroke crop: up to 2048 on either axis and 1,048,576 pixels.
- 2–1024 input samples; up to 60 seconds; nonmonotonic timestamps refused rather than repaired.
- 15-second worker wall-clock limit, with bounded process output and cancellation checks.
- At most 32 tagged desktop strokes in a document and a 128 MiB document-resource gate. Other existing cropped artwork is not falsely counted as desktop gestures, but its bytes still count toward the resource budget.

The stroke-count limit is an experimental safety boundary, not a satisfactory long-term solution for hatching or a full painting. Further work must address persistent logical paint layers and resource growth without silently merging layers or losing per-stroke undo. No claim of unrestricted continuous painting is made.

## Verification ledger

The unchanged worker has been checked with actual pinned Linux runs: pointy/dry preset pressure comparisons and 16 existing unittest cases. Those tests do not establish desktop UI acceptance. New adapter and QML results must be recorded separately after execution; configure-only success is not a compile or runtime pass.

Executed local stage checks:
- Fresh Release adapter target compiled; `Qt.MyPaintRasterCommand` passed (3.62 s), using the actual pinned pointy/dry worker and testing pressure/time binding, independent layer/group, Undo/Redo, save/recover, tagged-stroke cap, cancellation and invalid inputs.
- `Desktop.MyPaintUI` passed (2.28 s) in Linux Qt software/offscreen mode. Actual QML controls and injected tablet/mouse events exercised precise preview, acceptance, second gesture, pending-control rejection/rebinding, cancel, Undo/Redo, save/reopen, PNG/ORA export, tool-switch/focus cancellation and in-flight cancellation. Binding/reference/type diagnostics were zero.
- These are synthetic input events, not physical tablet or Windows verification. Full-suite status is recorded in the publication receipt after the complete build finishes.
