# Drawborad contour-flow experiment

Base inspected: DropNowOfficial/Drawborad PR 6, commit 4f2067871c7535100143eccc1c9ad85ad3820489. Reviewable experiment; not enabled in the application.

## Outcome and enablement

This is a deterministic, opt-in form-following pigment primitive, not automatic artistic planning. `StyleSettings::followContours` defaults to false. All current callers, including ReconstructionJob's base-pigment call, retain their existing behavior. A caller must explicitly pass true and use a verified persisted native binary mask (`PlanningRegion::clipToReviewedMask=true`). Otherwise the opt-in returns UnsupportedFeature. No UI setting was added.

The new planner uses temporary distance bands to trace connected inward contours, splitting only at native source-color/alpha variation. It removes exact collinear samples without shortcuts across holes. It emits ordinary engineVersion=2 brush events. No source pixels or planning raster are stored as paint. Existing mask validation, cancellation, event/sample budgets, color profile handling, native renderer and reconstruction validation are retained.

## Actual native-render results

Hollow-form fixture, same source/mask/2px marker/spacing/tolerance:

- Before: 72 strokes, 504 geometry samples.
- Opt-in after: 12 strokes, 66 geometry samples.
- Final rendered RGB is byte-identical. Alpha coverage exactly matches the reviewed mask; holes remain transparent.
- Source-asset reads: 0 in every native replay checkpoint.
- `The generated before/after contact sheet` shows native-rendered checkpoints at 0%, 25%, 50%, 75%, and 100% of stroke count, not wall-clock replay time.

Actual 256px cat source and its original two reviewed masks (base pigment only, guides excluded):

| Region | Baseline strokes / samples | Opt-in strokes / samples | Color MAE baseline → opt-in |
|---|---:|---:|---:|
| Background | 1641 / 12607 | 931 / 9569 | 0.00391957 → 0.00229353 |
| Cat | 3301 / 9146 | 4914 / 14623 | 0.0353099 → 0.0271871 |
| Total | 4942 / 21753 | 5845 / 24192 | See per-region results |

Both native cat renders have alpha MAE 0 and source-asset reads 0. Total event count increases 18.3%, despite lower color error. Therefore this experiment is not enabled by default and is not evidence that automatic cat reconstruction now resembles a human artist. Final seam handling avoids redundant pigment dabs; these final numbers supersede the earlier 5909-event intermediate result.

## Verification

- Original 13 E02 portable tests passed before editing.
- New hollow-form and rounded-silhouette regressions failed against baseline as intended.
- Final exact-source portable E02 executable: **18 cases, 0 failures**. Includes 32 deterministic concave-mask/island/thin-remnant cases over 4 nib widths, sharp pigment boundaries, deterministic replay, budget errors, native-mask requirement, locked-layer rejection and cancellation.
- Native C++ renderer produces all comparison pixels; Python only lays out the PNG contact sheet.
- `git diff --check` passed.
- Full project CMake/Qt workflow: **not run**. A configure attempt reports `cmake: command not found`; Qt6Core is also unavailable through pkg-config. This focused connector-fetched source checkout is not a full CI run. No browser or installed runtime was changed.

Portable command, from the patch folder or full repository:

```sh
c++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Werror \
  -fno-fast-math -ffp-contract=off -Isrc -Itests/support \
  tests/support/TestMain.cpp tests/reconstruct/PlanningTests.cpp \
  src/core/DocumentSpec.cpp src/core/Document.cpp src/core/CommandBus.cpp src/core/Digest.cpp \
  src/render/ReferenceRenderer.cpp src/render/CoverageRasterizer.cpp src/render/PixelBlend.cpp \
  src/reconstruct/ReconstructionService.cpp src/reconstruct/EventWriter.cpp \
  src/reconstruct/ContourPlanner.cpp src/reconstruct/RegionPlanner.cpp src/reconstruct/StrokeScheduler.cpp \
  -o /tmp/drawborad-test-E02
/tmp/drawborad-test-E02
```

For the hollow-form evidence executable, replace the two tests/*.cpp entry files with `tools/qa/contour_pigment_evidence.cpp`, then pass an output directory. The separate actual-cat benchmark used the preserved original 256px source and its two reviewed masks. The source PNG was converted to RGBA bytes without resizing or changing pixels. The cat fixture is not committed to this repository; these cat numbers are measured experiment observations, not a CI regression guarantee.

## Remaining architectural gap

The current Qt ReconstructionJob still creates guides/base/empty-detail placeholders and completes each object's guide/base before proceeding to the next object. Those files were inspected but are unchanged. The patch does not add scene-wide phase ordering, meaningful shadow/highlight/detail decisions, anatomical linework, learned artist reasoning, or a user-visible artist workflow. The scheduler still assigns fixed per-sample timing. A separate authored native control can demonstrate the engine's ability to execute those choices, but cannot establish that this generic planner makes them.

Do not enable this policy across production inputs based only on the hollow-form result. More complex images can increase event count, hit budgets, and need artist-level composition/color-region decisions beyond mask-following bands.
