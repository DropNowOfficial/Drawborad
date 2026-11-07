# Automatic stages: default-off research prototype

**This is not a released automatic artist workflow.** The existing Drawborad
planner, `StyleSettings::followContours`, Qt workflow, Windows setup and security
settings are unchanged. Nothing in production calls these scripts. The public
prototype CLI requires `--experimental`; its output still needs visual review.

## What is automatic

Given a reference and existing reviewed object masks, the prototype derives a
small source palette, aggregates spatially supported color regions, and builds
actual brush geometry. It orders the entire scene as:

1. `base`: broad, form-following block-in of the source's dominant object color.
2. `form`: other coherent color regions, including lighter/darker patches.
3. `detail`: continuous centerlines of sufficiently dark source structures,
   with nib pressure derived from their distance field.

These are appearance rules, **not** anatomical, lighting or semantic
understanding. The prototype has no model and no hand-authored cat paths. It does
not receive face/nose/eye/bell QA coordinates. A small number of legitimate
isolated marks can be dabs; low-contrast antialias fragments are deliberately
approximated instead of spawning thousands of pixel events.

Only nonempty stages get a layer. Each original object has at most three layers,
all sharing that object's original persisted binary mask. Color components are
temporary planning data, not new objects, persistent color masks, raster fills or
source overlays. `NativePlan.cpp` uses the unchanged reconstruction validator,
document commands, project store and `ReferenceRenderer`. It saves/reopens a real
`.draw`, clears source pixels before replay, and exports native RGBA plus an audit.

## Honest result: promising on one flat cartoon, not generally solved

The frozen v6 cartoon result has **153 real events / 9,709 geometry samples / four
nonempty layers** across two objects. Global stage counts are 49 base, 74 form,
30 detail. Paths shorter than 3 pixels: 5 base, 4 form, 0 detail. Median lengths
are 72, 14.04 and 29.54 pixels respectively. By comparison, an early prototype
had 1,951 events, most of them tiny color fragments. The final change is geometric
and spatial, not layer renaming or count relabeling.

Independent native-pixel evaluation found cartoon RGB MAE **0.011288** versus the
old application's **0.014650**, with exact alpha and no mask leakage. However,
small features still change: the pink triangular nose becomes a brown dot,
the bell color shifts, and eye/mouth dark-pixel recall remains about 92.9%/88.1%.
Fewer strokes are not proof of faithful details or human-like painting.

The same frozen parameters ran on seven independent inputs twice. Native
geometry/colors and final RGBA were identical between repeats; resources were
Mask-only, source-asset reads were zero, and save/reopen succeeded. **That is
execution/reproducibility evidence, not seven quality passes.**

Known failures include:

- Transparent geometric artwork: 1,102 events, 871 short paths, 26 completely
  missing visible pixels and full-canvas alpha MAE about 0.00935.
- Smooth shaded sphere: visible bands/highlight discs, 158/208 short paths,
  29 completely missing visible pixels and alpha MAE about 0.00209.
- Translated/scaled sphere: the same failure class, 136/181 short paths and
  12 completely missing visible pixels. This was not repaired with coordinates.
- Genuine cat photograph: 1,388 events, RGB MAE about 0.03886 and edge F1 about
  0.6124. Fur/whisker detail is lost and the green eyes change toward brown.
- Existing 64px gradient fixtures: low event count but visible color blocks.

The v6 outputs are retained as negative evidence. Do not advertise photography,
continuous shading, soft-alpha artwork or arbitrary colored images as supported.

## Input contract and explicit rejection

Inputs are color-managed, row-major **RGBA8** bytes plus existing, nonempty,
nonoverlapping binary U8 masks, one byte per pixel (0 or 255). Raw RGBA carries no
ICC metadata; connecting this to production requires the normal color-management
and reviewed-source provenance flow. The prototype does not replace segmentation.

Limits: 1–512 pixels per dimension, 1–16 objects, 2,048 total events, 250,000 total
samples, nib width at most 128. File sizes and native sample/segment bounds are
checked. Native footprints remain clipped by the original object mask. These
resource and boundary checks also apply to diagnostic runs.

`contract.py` adds conservative, observable eligibility checks per object:

- Every reviewed source pixel must be fully opaque. Fractional alpha, or
  transparent source pixels included in a mask, are rejected.
- At least 80% of pixels must lie in the 16 most frequent RGB bins of size 16.
- At most 35% of interior pixels may have nonzero RGB neighbor variation no
  greater than 6/255, a warning sign for broad smooth gradients.

This is a **routing heuristic derived after the seven-fixture evaluation**, not
an independently validated photo/illustration classifier or a quality guarantee.
It currently admits the original cartoon and rejects the six demonstrated
unsupported cases. It can reject suitable art or admit unsuitable art. Passing
preflight still leaves `qualityStatus=not-evaluated` and
`naturalProcessVerified=false`; visual/detail review is required.

Rejected inputs get `preflight.json` with measurements/reasons and exit code 3.
No brush plan is emitted. Inputs are never cropped, resized, made opaque, omitted
or silently sent to another planner. `--diagnostic-unsupported` explicitly allows
negative testing; its report is labeled `diagnostic-unsupported-input`, never an
accepted painting. It must be combined with `--experimental`.

## Build and run

Requires C++20 and Python with NumPy >=1.26,<3 and SciPy >=1.11,<2.
The minimums follow API compatibility review; local execution used NumPy 2.3.5
and SciPy 1.17.0. Lower-version runtime compatibility still needs CI evidence.
No Qt SDK, network, model, Windows machine or public port is required. Optional
dependencies are listed in this folder's `requirements.txt`; no installation is
performed by the scripts.

From the repository root:

```sh
bash tools/experimental/automatic_stages/build_native.sh /tmp/drawborad-staged-native
python tools/experimental/automatic_stages/plan.py --experimental \
  --rgba /path/source.rgba --width 256 --height 256 \
  --mask /path/object-0.mask --mask /path/object-1.mask --output /tmp/cat-plan
/tmp/drawborad-staged-native /tmp/cat-plan/plan.txt /path/source.rgba \
  /tmp/cat-native /path/object-0.mask /path/object-1.mask
```

Outputs include `automatic.draw`, native `base.rgba`, `form.rgba`, `detail.rgba`,
`final.rgba`, a transparent `zero.rgba`, replay checkpoints and `audit.json`.
RGBA files retain the original dimensions and alpha; they are not white-matted.
An absent phase has no empty layer, and its checkpoint repeats prior contents.
Output directories must not already exist. The native runner reads source/masks
at their expected bounded byte sizes and rejects trailing or truncated bytes;
the text plan is limited to 16 MiB.

## Reproduce checks and independent fixture runs

```sh
DRAWBORAD_NATIVE_PLAN=/tmp/drawborad-staged-native \
  python -m unittest discover -s tests/experimental -v
python tools/experimental/automatic_stages/run_evaluation.py \
  --manifest /path/manifest.json --native-runner /tmp/drawborad-staged-native \
  --output /tmp/eligible-runs
```

The 14 regression/integration tests cover diagonal continuous ink, genuine dots,
low-contrast noise, local color support, scene-wide order, translation,
determinism, mask/budget rejection, native roundtrip, oversized input, explicit
opt-in, unsupported alpha, invalid manifests and explicitly missing runners.
An explicitly set `DRAWBORAD_NATIVE_PLAN` must name a runner file or the native
test fails. Only an unset variable with no local default runner permits a skip;
CI should set the variable to its freshly built runner.

Each manifest fixture supplies `id`, `width`, `height`, `rgba` and `masks` paths,
relative to the manifest. The `fixtures` list must be nonempty, IDs must be unique
safe local names, dimensions must be integers in 1..512, and each fixture must
supply 1..16 mask paths. Invalid manifests exit 2 before creating outputs or
starting any fixture. QA-only ROIs are not passed into planning. The batch runner
independently plans/imports/renders twice, retains both audits, and records
native geometry/color digests and byte identity. It prints `EXECUTED ... quality
not evaluated`, labels `runs.json` execution-only and exits nonzero on any
execution/preflight failure. Quality failures belong in a separate evaluation
report and are not converted into fake runtime failures or successes.

To reproduce known v6 failure cases deliberately, add
`--diagnostic-unsupported` to the batch command. Original v6 source bytes and
hashes, pre-hardening outputs and independent reports are preserved in the review
artifact, separate from this safer public entrypoint. Eligibility/resource
hardening does not change the frozen planning algorithm or its image parameters.

The portable existing E02 suite also remained at 18/18. Full CMake/Qt/OS CI was
not run in the research workspace: CMake and Qt6Core were unavailable. This
prototype is not wired into the project's aggregate CMake test suite; run the
commands above explicitly before considering integration.

## Integration still required

The Qt workflow currently permits only `guides/base/detail` result stages and
creates placeholders/object-local schedules. Real integration must explicitly
support `form`, create only used stages, schedule phases across all objects,
preserve source provenance/color profiles, expose a deliberate experimental
choice and display preflight plus visual-quality failures. Do not disguise form
strokes as guides, create one object per color fragment, silently enable the mode
or weaken native validation to fit this prototype. The shipped default remains
unchanged until a separately reviewed product decision.
