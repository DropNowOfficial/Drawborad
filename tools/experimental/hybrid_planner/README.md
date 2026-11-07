# Experimental low-budget hybrid corrections

This is a **default-off, unintegrated research tool** for opaque, compact-palette
flat artwork with existing, reviewed, nonoverlapping binary object masks. It
preserves the `automatic_stages` v6 plan and appends a small number of measured
native-rendering corrections. It is not a semantic artist model, a general
photo/gradient planner, or an accepted replacement for the application planner.
Nothing here changes the stable planner, its defaults, UI, or safety budgets.

## Method and scope

1. Generate the unchanged v6 palette/geometry plan. Preserve its actual serialized
   colors, widths, samples, event order and object/phase assignment. Replay it
   through the real C++ coverage and pixel-blending code.
2. Compare the resulting native pixels with the reference. Rank connected source
   color regions using residual energy, not facial coordinates or named features.
   Search at most 512 seeds, retain at most 96 traceable regions. Tiny antialias
   fragments do not consume the retained-region limit. Floating-point filter
   roundoff is clamped before square root, and nonfinite scores are rejected.
3. Propose medial and contour paths inside those regions. Evaluate their actual
   native coverage, fit one constant pigment/opacity per stroke, and commit only
   positive native loss improvements. The temporary color regions never become
   saved objects, user-visible masks, or raster resources.
4. Append accepted corrections to the detail phase, after all original phases.
   Each new path is at least 3 pixels long. The untouched v6 prefix still contains
   some short marks. We do not rename such marks or claim they are continuous.

A genuine source dot can be too small for the continuous-correction contract and
remain wrong. The diagnostic chroma-extremum helper does **not** bypass the new
minimum path length. The fixed budgets are **2,048 total events, 250,000 samples,
192 appended events, 16 objects, 512x512 pixels, and a 128-pixel native nib**.
The initial baseline must already fit. There is no automatic budget expansion,
resizing, alpha flattening, or fallback to another planner.

Color region masks are only planning geometry. Actual coverage is clipped by the
same reviewed binary object masks used by the baseline. Source alpha is never
saved as a coverage mask. No source image is embedded in the native project.
Soft/nonopaque reviewed pixels are rejected, including in diagnostic mode.
`--diagnostic-outside-base-contract` only permits explicit opaque palette/gradient
negative tests; it is not a quality-support claim or an alpha bypass.

## Results and remaining failures

The algorithm was frozen as hybrid v3 before two independent synthetic tests
were revealed. No QA region coordinates were used by the algorithm. Packaging
then added input validation and repository import paths without changing the
algorithm or its parameters.

- Original 256x256 cartoon: v6 **153 events / 9,709 samples**, hybrid **345 events /
  20,569 samples**, including 192 appended paths. New minimum path length is 3px,
  median 22.95px, with no consecutive duplicate points. Native RGB MAE improved
  from **0.01128791 to 0.00719457**. The small pink nose and yellow bell are visibly
  improved. Five rendered layers are nonempty; phases remain globally ordered.
- Independent unseen synthetic A: baseline 101 events, hybrid 284 (+183). RGB
  error fell 49.3%, and the preregistered rare-color regions passed. However,
  edge F1 **0.966415 is below the fixed 0.97 target**. This is partial improvement,
  not a fully passed quality result.
- Independent unseen synthetic B: baseline 78 events, hybrid 270 (+192). RGB
  error fell 72.0%, and the preregistered numerical checks passed, including
  recovery of two rare-color regions that failed in the baseline. This does not
  establish general painting quality.

The two synthetic cases had identical actual events/layers and exported native
frames across two runs. Independent v6 projects match the hybrid baseline pixels
and full event prefix. Real within-stroke replay ends at the native final image.
Source reads are zero during rendering and persisted resources are masks only.
These are execution/resource facts, not proof of human-like planning.

Both synthetic inputs are now **seen regressions**, not fresh holdouts for later
iterations. Do not retune this frozen version to make A cross its threshold.
Photo fur, continuous gradients, soft-alpha artwork, and generalization beyond
this narrow input contract remain unsupported here. Positive aggregate pixel
scores cannot substitute for local color, contour, process, and visual review.

Historical experiments remain separate: v6 alone lost small rare colors; the
flow experiment improved gradients/photo structure but used thousands of strokes
and did not preserve fine fur or soft alpha. This hybrid deliberately does not
inherit the flow planner's 8,192-event research budget or run its stroke planner.
It reuses only the checked native feedback interface and constant-color fitting.

## Build and run

From the repository root, with C++20 and the Python dependencies available:

```sh
python -m pip install -r tools/experimental/hybrid_planner/requirements.txt
bash tools/experimental/hybrid_planner/build.sh
PYTHONDONTWRITEBYTECODE=1 python -m unittest discover \
  -s tools/experimental/hybrid_planner -p test_hybrid.py -v
```

The build compiles the native hybrid importer/renderer and rebuilds the sibling
flow feedback library on every explicit build so a stale binary is not reused. It uses existing repository modules;
there are no downloaded models or hidden inference services. `DRAWBORAD_SOURCE`
can select the repository source root for a standalone research checkout.
`DRAWBORAD_HYBRID_NATIVE` can select a test runner. A missing explicitly selected
runner fails tests; no tests are silently skipped.

```sh
python tools/experimental/hybrid_planner/hybrid.py --research \
  --rgba /input/source.rgba --width 256 --height 256 \
  --mask /input/object-0.mask --mask /input/object-1.mask \
  --output /new/plan

tools/experimental/hybrid_planner/build/native-hybrid \
  /new/plan/plan.txt /input/source.rgba /new/native \
  /input/object-0.mask /input/object-1.mask
```

Raw input is row-major RGBA8 and one same-sized U8 0/255 mask per object.
The programmatic API validates the same shapes, types, masks and eligibility,
normalizes noncontiguous arrays, and rejects malformed inputs before planning.
CLI reads are bounded to the expected byte sizes. Existing output paths are
never overwritten. The native writer requires exclusive directory creation
immediately before writing, closing the earlier check-then-create race.

The importer accepts `DBHYBRID1 width height objects totalEvents baselineEvents`.
The baseline count is a declaration, not native proof that v6 generated that
prefix. It enforces at most 192 events after that boundary; all must be detail
paths of at least 3px with no consecutive duplicate positions. Independent
baseline/audit comparisons, not the header, prove an unchanged v6 prefix.
The importer also rejects nonopaque source pixels inside the reviewed masks,
then compiles each proposed batch through the original
`ReconstructionService`, saves and reopens an actual `.draw` project, clears the
source bytes, and renders with `ReferenceRenderer`. Outputs include native
`automatic.draw`, actual event/layer `audit.json`, zero/base/form/detail/final
RGBA frames, and three native event-prefix replay frames. These snapshots stop between
strokes, not within them; the separate independent timeTicks replay cited above
is the evidence for within-stroke animation. `feedback.rgba` is only
an intermediate cross-check and must byte-match the separately imported final.

To execute a fixture manifest twice without quality claims:

```sh
python tools/experimental/hybrid_planner/run_evaluation.py \
  --manifest /input/manifest.json --output /new/evaluation
```

Optional `--id` arguments select fixtures. The manifest requires a nonempty
`fixtures` list, unique safe IDs, dimensions, an RGBA path and mask paths; relative
paths resolve against the manifest directory. Invalid/empty selections fail.
Unsupported inputs or execution failures produce logs and `runs.json`, return
nonzero, and are never silently replaced with another method. Successful output
says `EXECUTED ... quality not evaluated`, and reports actual repeat equality.

## Verification record

**15 automated tests** cover finite residual scores, color/antialias candidate
rules, traceable small shapes, seed starvation, malformed and noncontiguous
buffers, soft-alpha rejection, opt-in and file-size checks, no-clobber, event and
sample caps, native opaque-alpha and declared-correction boundary checks, mask
holes, repository imports, empty manifests, and actual native
save/reopen/render equality. They passed locally with no skips. This does not
claim that every fixture meets its quality target.

Research evidence, kept outside the source package:

- Frozen algorithm SHA256: `0d4abdd5f703de81503750b0317a616b4fe1c05830e1f151857b70d28221a92b`.
- Frozen source/dependency/binary manifest SHA256: `5dc28d70786aabff41a84ecefee139f03a2d41ee9cec3598798927fa4e8fbb25`.
- Original-cat final native RGBA SHA256: `775fbe70c1a9b54078a25301290b336a45b30d3acfe0f164375bb1052a7d6a03`.
- Packaging recheck: all 345 actual events, layers and all eight native RGBA
  frames exactly match the frozen version. No rendered evidence was overwritten.

The parent's separate OS CI is required before any publication decision. This
standalone research environment did not build the full Qt application. The
stable reconstruction workflow remains unchanged regardless of these tests.
