# Native-feedback flow planner research (default off)

This is an **unintegrated, explicit research tool**, not a replacement for the
stable reconstruction planner or the earlier palette/contour experiment. It
uses image measurements, not semantic understanding of an artist's decisions.
No production default, Qt workflow, Windows setting, renderer, or security rule
is changed by these files.

The current v5 candidate improves continuous shading, local photo colors,
whiskers and some fine structures. It **does not solve realistic fine fur,
efficient cartoon planning, or transparent material reconstruction**. A successful
run means the native document was executed and verified, not that visual quality
was accepted. No new model or external executable is downloaded.

## Input contract and limits

- Explicit `--research` is required.
- Raw RGBA8, original dimensions, each side 1..512 pixels. This standalone tool
  does not provide a full color-profile/provenance conversion workflow.
- 1..16 already-reviewed binary U8 object masks, each exactly image-sized,
  nonempty and nonoverlapping. Every accepted source pixel must be fully opaque.
  A transparent background outside all reviewed masks is permitted.
- Fractional or zero alpha **inside** a reviewed mask is rejected before a plan
  or native output is written. No cropping, flattening, resizing, silent fallback
  or opaque backing layer is used to conceal the rejected input.
- `--diagnostic-soft-alpha` explicitly permits negative research tests only. Its
  plan is labeled `diagnostic-soft-alpha`; it does not promise supported alpha.
  The batch command does not use this override.
- Hard research limits: 8,192 events, 500,000 geometry samples, 128-pixel maximum
  nib, 32 MiB text plan. These limits are local to this new research runner. The
  old experiment's 2,048-event/250,000-sample contract and stable defaults stay
  unchanged. Exceeding a hard limit rejects the plan.
- Width-level allocation limits are recorded in plan statistics. Reaching one
  ends that level with unresolved error; it is not convergence or quality
  approval. The final ridge allocation also respects the remaining event budget.
- RGBA/mask files are read at their checked expected size, with at most one extra
  byte to detect changes. Existing output directories are never overwritten.

## Actual algorithm and native representation

1. Draw a geometric underpainting with genuine, connected native contour paths.
   No raster fill is inserted. Original object masks remain the only masks.
2. At decreasing widths 32/16/8/4/2/1.2, use smoothed RGB structure tensors to
   trace local isophote directions, with direction-sign agreement, bounded
   length and local color consistency. All line segments must remain within the
   original reviewed object mask.
3. Select candidates from current rendering error. Native whole-stroke coverage
   is computed by Drawborad `strokeCoverage`; native `blendCoverage` and the same
   layer composition produce the trial image. Fit a constant local stroke color
   and opacity, then accept only a measured native improvement. There is no
   global color palette or per-pixel event correction pass.
4. In the detail phase, add source-derived bright/dark Gaussian-Hessian line
   centers. Reject weak or one-sided features, preserve polarity and mask
   boundaries, and taper the pressure along continuous paths. A line must be at
   least 3 pixels long. This is not semantic identification of a whisker.
5. v5 adds source-aligned native gradient error to the detail objective. It
   slightly improves fine structure over v4 but worsens aggregate photo color
   error a little. It is not a physical pigment model or an arbitrary sharpening
   operation. Every final stroke still has a native, constant color.

All objects' base events precede all form events, which precede detail events.
Only nonempty native layers are created, up to three per original object. The
stages describe scale and geometry, not inferred subject anatomy. In particular,
`form` is not falsely labeled `guides` to bypass the production result validator.

Native `ReconstructionService::compile`, `Document::apply`, `ProjectStore`
save/recover, `ReferenceRenderer` and `nativeMaskedTile` remain authoritative.
Each final sample and each quarter-pixel segment check must stay inside the
original reviewed mask. Temporary source analyses never become document assets.
The source bytes are cleared before replay. Persisted resources are only `Mask`;
`audit.json` reports source reads, events, samples, widths, colors, hardness,
opacity, timestamps, layers and native geometry/color digest.

The feedback canvas and final native export are compared byte-for-byte. Planning
feedback is not delivered as a substitute for native rendering.

## DBFLOW1 text contract

This new schema does not change `DBPAINT1`:

```
DBFLOW1 width height object_count event_count
object_index phase_index width hardness opacity r g b alpha sample_count
x y pressure
... sample_count rows ...
... remaining events ...
```

Phase indices are 0=base, 1=form, 2=detail, globally nondecreasing. Positions,
pressure, colors, hardness and opacity must be finite and in their native ranges.
The renderer owns interpolation, pressure-dependent radius, soft coverage and
alpha blending. No coverage raster or reference image is encoded in this format.

## FFI boundary

The Python wrapper accepts only positive, bounded RGBA8 and same-sized binary U8
masks. Noncontiguous views are normalized to C-contiguous arrays before passing a
pointer. Geometry is a bounded finite N x 3 array; colors contain exactly four
finite unit channels. The C ABI independently checks supplied byte/value counts,
brush ranges, points, mask values and segment samples.

The native candidate/evaluate/commit state machine invalidates stale pending
work on an invalid call. Commit requires a successful current evaluation. Phase
changes invalidate pending work. The wrapper rejects operations after close.
Returned Python pixel arrays own their memory rather than exposing freed native
storage. The private C ABI requires valid caller-owned buffers and an owned
handle; it is not an interface for arbitrary foreign memory addresses.

## Build and test

Linux C++20, Python, NumPy >=1.26,<3 and SciPy >=1.11,<2 are required. The lower
bounds reflect API review; this research workspace ran NumPy 2.3.5/SciPy 1.17.0.
Pillow >=9 is optional for the PNG inspection helper only. No installation is
performed by a script.

When these files are under `tools/experimental/flow_planner` in the repository:

```sh
bash tools/experimental/flow_planner/build.sh
python -m unittest discover -s tools/experimental/flow_planner -p test_flow.py -v
python tools/experimental/flow_planner/flow.py --research \
  --rgba /path/source.rgba --width 451 --height 300 \
  --mask /path/reviewed-object.mask --output /tmp/flow-plan
tools/experimental/flow_planner/build/native-flow \
  /tmp/flow-plan/plan.txt /path/source.rgba /tmp/flow-native \
  /path/reviewed-object.mask
```

For a standalone research checkout, set `DRAWBORAD_SOURCE` to the existing
Drawborad source root before running `bash build.sh`. The build uses unchanged
native core/render/reconstruction/storage modules and compiles with `-Werror`,
`-fno-fast-math` and `-ffp-contract=off`.

The 15 tests cover line-center tracing and original-mask holes, no invented
ridges in a flat field, native alpha and gradient objectives, native save/reopen
and full-phase replay equality, local-color gradient determinism, noncontiguous
arrays, malformed FFI arrays, wrong call order and stale pending buffers, bounded
input, exclusive output-directory ownership during concurrent creation, soft-alpha refusal and empty/invalid batch manifests.

```sh
python tools/experimental/flow_planner/run_evaluation.py \
  --manifest /path/manifest.json --output /tmp/flow-regressions \
  --id original-cat --id chelsea-photo --id e02-gradient64 \
  --id e02-split64 --id holdout-curved-chroma
```

The manifest has a nonempty `fixtures` list. Each entry supplies a unique safe
`id`, integer dimensions, an `rgba` path and 1..16 `masks` paths relative to the
manifest. QA ROIs are not sent to the planner. Each selected fixture is planned,
saved, reopened and rendered independently twice. Actual event/layer arrays and
eight native RGBA frames are compared. Execution failures exit nonzero; stdout
says `EXECUTED ... quality not evaluated`, never visual-quality `PASS`.

`automatic.draw`, transparent `zero.rgba`, cumulative base/form/detail/final
RGBA, replay checkpoints and `audit.json` are native outputs. The PNG helper only
encodes those verified RGBA bytes; it never paints or repairs the image.

## Evidence and honest limits

Independent evaluation was separate from the planner. No face coordinates, QA
ROIs, manually authored events, or modified source pixels were supplied to it.

- Frozen v3: all seven original fixtures plus two then-unseen holdouts were run
  twice. Actual events/layers and 72 frame pairs were identical. Repetition is
  verified execution, not quality acceptance.
- Gradients improved substantially, including the then-unseen curved-chroma
  holdout: RGB MAE 0.003687, gradient-vector error 0.000863. That exposed image is
  now only a regression input, not a new holdout.
- v3 soft-ribbon failed badly: alpha MAE 0.008886, soft-edge MAE 0.160969, visible
  ragged transparency. Sphere and flat-geometry soft edges also failed. These
  negative artifacts remain frozen; the current default entry refuses them.
- Chelsea v5: 7,314 strokes / 149,605 samples / 3 layers, no paths below 3 pixels,
  alpha error zero. RGB MAE 0.017989 (v6 palette experiment: 0.038857), edge F1
  0.882456, fine-texture correlation 0.803161. Green/gold eyes, the red nose and
  some continuous white whiskers are retained.
- Nevertheless, fine texture energy is 0.786482 of the reference, still below
  the pre-registered 0.8 target. Large regions have coarse curved fur marks,
  over-smoothed patches and some floating fine lines. It is not faithful fine
  fur reconstruction. v4 color MAE 0.017493 and chroma error were slightly better
  than v5, so that color comparison is retained rather than silently replaced.
- The cartoon takes over 4,000 events in pure flow versus 153 in the earlier v6
  palette/contour prototype. Better facial color/line fidelity does not excuse
  this large efficiency regression or establish human-like artistic intent.

Final opaque regression execution (two independently planned native runs each):

| Input | Events | Samples | RGB MAE |
| --- | ---: | ---: | ---: |
| Original cartoon | 4,448 | 84,522 | 0.00731718 |
| Chelsea photograph | 7,314 | 149,605 | 0.01798910 |
| E02 gradient | 200 | 1,636 | 0.00758170 |
| E02 split | 238 | 3,177 | 0.00309349 |
| Previously seen curved chroma | 890 | 22,154 | 0.00367324 |

Independent checking found zero alpha error for these accepted pixels, exact
native events/layers and 40 matching RGBA frame pairs. The photograph also
matches the frozen v5 image before input/FFI hardening. All four previously
failing soft-alpha fixtures were explicitly refused with no plan/native output.
The cartoon/photo efficiency gate and the photograph's finest-texture gate
still fail.

The current five opaque inputs are fixed regressions, not a representative
quality certification. Native interoperability, replay, source independence and
repeatability are distinct from aesthetic quality. The 2,048-event integration
and efficiency goal has not been met. The 8,192 research ceiling is not a new
production allowance.

Full Qt/CMake and multi-OS CI were not run in this research workspace. These files
are not wired into the product or aggregate CMake test suite. Production
integration would need explicit stage support, user opt-in, source profile and
provenance handling, rejection UX, and separately reviewed performance limits.

## Next structural experiment, not implemented here

Preserve the low-event flat-region/contour plan for illustrations, then allocate
only a small, explicit budget to continuous local-color and line corrections
selected from real native residuals. This could restore rare small colored marks
without expanding a simple cartoon to thousands of strokes. Candidate selection
must remain coordinate-free and must not use the evaluation ROIs. For photos,
line crossings, multi-scale ridge connectivity and stroke allocation need work;
simply increasing the palette or event ceiling is not a demonstrated solution.

## Method references

These are inspiration, not claims that the full published methods are reproduced:

- Hertzmann, 1998: multi-scale, constant-color curved painterly strokes and washes.
  https://mrl.cs.nyu.edu/publications/painterly98/hertzmann-siggraph98.pdf
- Kyprianidis and Döllner, 2008: color structure tensors and coherent directions.
  https://www.kyprianidis.com/p/tpcg2008/jkyprian-tpcg2008.pdf
- Steger, 1996/1997: Gaussian-derivative line-center detection and its limitations.
  https://mv.in.tum.de/_media/members/steger/publications/1996/eccv-96-steger.pdf
