# Experimental reference-derived native region trace

This is a rejected artwork candidate / reproducible engineering experiment, **not
an accepted final cover, hand redraw, or high-resolution restoration**. No image
model is used. The source is quantized and traced into horizontal native color
runs; it is not embedded as a bitmap in the resulting document. This is still
reference-derived tracing, not newly authored artistic detail.

## Versions and input

- `cover_region_plan_v3.py` is the authoritative, fully self-contained v3 script.
- `cover_region_plan.py` currently contains identical v3 code (historical filename).
- `cover_region_plan_v2.py` preserves the older v2 experiment. It is **not** v3.
  Its older title/silhouette names overstate the semantic separation; do not use
  those names as evidence about v3.
- `BuildCoverRegions.cpp` consumes the textual plan and writes a real native
  `.draw`, PNG and streaming layered ORA, then verifies saved/reopened pixels.

The privately supplied reference is the approved black-background cover,
`41FACF60-871C-4DE8-A553-C60B1A01D164.jpeg`, exactly 1024 x 1536 pixels.
Supply it locally; do not commit the reference image or generated artifacts to
this repository. Spatial polygons are specific to that composition, not a
universal segmentation algorithm. An unrelated image of the same size will not
produce meaningful semantic layers.

## Reproduce from the repository root

Prerequisites: Python 3 with Pillow, C++20 compiler, real libpng headers/library.
The command below uses the local verified header location and system library;
substitute their actual installation paths on another machine. No Qt is needed.
Do not compile with `-DNDEBUG`: this experimental tool uses assertions for input
and result checks. It is not a hardened untrusted-plan importer.

```sh
g++ -std=c++20 -O0 -Wall -Wextra -Wpedantic -Werror \
  -DDRAWBORAD_HAS_PNG -Isrc -Iout-libpng-headers \
  tools/artwork/BuildCoverRegions.cpp \
  src/ui/{LayerStream,MergedDocument}.cpp \
  src/core/{LineartRaster,Document,DocumentSpec,CommandBus,Digest}.cpp \
  src/render/{ReferenceRenderer,CoverageRasterizer,FineLineCoverage,DryInkCoverage,ChiselInkCoverage,PixelBlend}.cpp \
  src/storage/{ProjectStore,ProjectCodec,FileIO,Journal}.cpp \
  src/codecs/{OraStream,ZipStream,RasterWriters,AtomicExport}.cpp \
  /usr/lib/x86_64-linux-gnu/libpng16.so.16 -o /tmp/build_cover_regions

# Output directory must NOT exist. The Python script creates it.
python3 tools/artwork/cover_region_plan_v3.py /path/to/private-cover.jpeg /tmp/cover-v3-new
/tmp/build_cover_regions /tmp/cover-v3-new
```

Outputs: `plan.txt`, `plan-receipt.json`, `cover_candidate.draw`,
`cover_candidate.png`, `cover_candidate.ora`, `native-receipt.json`.
300 DPI is an export metadata setting, **not** proof of assignment compliance or
new detail beyond the 1024 x 1536 source.

## Exact v3 parameters, overrides, and names

There are no shell-time patches, temporary-script overrides, or external
parameter files needed. The v3 source includes all polygons and these decisions:

- 17 RGB colors, explicitly listed in `palette`.
- Grayscale thresholds: 35, 50, 67, 85, 107, 128, 148, 168, 188, 208, 222.
- Paper is RGB (246,239,225). Paper-class pixels are omitted except inside the
  approximate missing-person polygon (region 8), where opaque paper is kept.
- Red classification is `r-g > 32`, `r > b*1.18`, `r > 90`; red pixels override
  the spatial region and go into DELETE red accents. Red thresholds are
  140,168,194,220; this also captures the red corner marks, not only the button.
- Low-contrast local smoothing uses original grayscale 3 x 3 min/max range <24,
  and only photo/darkroom regions 3/7 with RGB-average strictly between105/215,
  or already-classified red pixels. It uses a radius1 box average. Classification
  as red is decided **before** averaging. Faces/black/high-contrast edges are not
  globally denoised. 119370 pixels meet this rule for the exact reference.
- Runs are horizontal, half-open `[x0,x1)`, grouped by (region,color), split at
  60000 runs/event. Native engine8 bucket events, max1000000 geometry points.

Native bottom-to-top region names (each can contain multiple palette colors):
1. Framing dry black field
2. Upper face and rough hair
3. What sister speech
4. Photo boy and missing sister
5. Father face fine marks
6. Hand and finger contour
7. Title zone with knockout lettering
8. Bottom right darkroom
9. Missing sister approximate paper region
10. DELETE red accents

ORA lists these in reverse order above the separate Paper layer (11 total).
These names match v3 plan/audit; they are approximate hand-bounded regions, not
perfectly extracted character objects. The title layer contains black around
knockout letters, not editable text. Father/hand layers contain neighboring black
marks. The missing-person polygon includes paper and some outline. Red accents
include unrelated corner marks. Do not describe this as ten clean character layers.

## Known visual losses and reproducibility

Dark fields remain too solid, photo/darkroom gray is posterized, red button is
mottled, and source paper grain is removed. Broad black and fine marks are
preserved better than skeletonizing the whole image, but dry-brush continuity,
subtle tones and original character detail are not faithfully reconstructed.
Native runs are pixel geometry, not smooth scalable contour curves. Upscaling
these would not restore high-resolution detail. No claim about removal/absence
of hidden marks in the input is made. ORA does not preserve native layer locks
or every native project property; no external-editor check was run for this cover.

On 2026-11-12, v3 was rerun into a new empty output directory with the original
private JPEG. It produced 105 events, 694938 geometry points, no embedded image
resources, and the same `plan.txt` and PNG bytes as the reviewed v3 candidate.
The native tool also checked save/reopen pixel equality. PNG SHA256: `48afa24860616e9b854edf07305a8ecfe5824c59141399da983bc4c9b973cd22`.
Repository source is the reproduction recipe, not a distribution of the private
reference. The rerun used the already-built native tool; no GUI acceptance is
implied.

## Separate manually authored eye study (not v3 region tracing)

`eye_study_plan.py` reads no image pixels. It explicitly authors short native curves
from visual observation of the black cover's upper eye, brow, hairline and nearby
ear/tension marks. Design coordinates become sampled geometry on an actual
1600 x 1000 canvas, not enlarged pixels. `BuildFaceStudy.cpp` uses engine10 AA for
widths <=8 px and unchanged engine2 marker for broader strokes. Four independent
native event layers plus Paper, editable geometry; not a pasted reference.

```sh
python3 tools/artwork/eye_study_plan.py /tmp/eye-study-new
# Use the exact compiler/source list above, replacing:
# tools/artwork/BuildCoverRegions.cpp -> tools/artwork/BuildFaceStudy.cpp
# -o /tmp/build_cover_regions -> -o /tmp/build_face_study
/tmp/build_face_study /tmp/eye-study-new
```

Produces `eye_study.draw`, `eye_study.png`, `eye_study.ora`, and native receipt.
The source crop used only for visual assessment is (35,40,660,330) in the private
1024 x 1536 cover. This small study is not final artwork or a faithful recreation
of the original artist's hand. Wider marker marks retain existing renderer
behavior; broad curves can be too smooth, with insufficient broken dry-brush
texture. Pupil/eyelid proportions and hair masses require visual assessment,
not a test-count-based quality claim. No external-editor validation is claimed.

Eye-study v2 review: 55 events / 10808 geometry points, actual native1600x1000.
Saved/reopened pixels equal. Manual drawing remains rejected: hair is smooth
ribbon shapes rather than connected dry-brush masses; pupil is crescent-like
instead of the reference tilted oval; eyelids too regular; sparse composition
and uniform long taper lose the fearful expression. Denser curve sampling
improves old-marker tip dotting but some stamp artifacts remain. This is not
ready to extend across the full artwork. `eye_study_plan_v1.py` preserves the
earlier 45-curve experiment; current script generates the reviewed v2.


### Eye-study v3 (strictly local correction)

Current `eye_study_plan.py` generates v3; `eye_study_plan_v2.py` preserves v2.
V3 replaces the shifted-crescent pupil with short chords of an actual tilted
ellipse (radii10.5/20 in design coordinates, angle-24deg) and two highlights.
It changes upper/lower lids and the right eye corner, and authors a connected
upper-left polygonal hair mass with three intentional paper cuts. The hair
polygon is manually specified, not derived by thresholding source pixels.
It is sampled into overlapping horizontal native short curves, not embedded
bitmap data. No random noise or paper texture was added as fake dry-brush.
The study-only native tool accepts up to511 events; core budgets are unchanged.
This is still a geometry study, not a claim of finished artistic rendering.

## Dry-ink renderer comparison (separate from eye redraw)

`BuildDryInkStudy.cpp` calls the real coverage renderer for identical curves,
pressure and widths48/18/5px: legacy marker left, opt-in engine12 dry-ink right.
It writes a lossless1024x512 PPM; converting that PPM to PNG is encoding only.
This comparison is renderer output, not a GUI screenshot or final artwork.

```sh
g++ -std=c++20 -O0 -Wall -Wextra -Wpedantic -Werror -Isrc \
 tools/artwork/BuildDryInkStudy.cpp \
 src/render/{CoverageRasterizer,FineLineCoverage,DryInkCoverage,ChiselInkCoverage,PixelBlend}.cpp \
 src/core/{Document,DocumentSpec,CommandBus,Digest}.cpp -o /tmp/build_dry_ink_study
/tmp/build_dry_ink_study /tmp/normal-vs-dry.ppm
```

Fixed engine12 recipe: pressure controls width and ink deposit, 2.3px directional
fiber pitch, small slow along-path waviness and ink starvation; persisted seed
controls phase, not canvas random dots. Opacity still applies at normal blending.
32Mi pixel-visits/tile, max512px tile, max4096 samples, width.25..128px.
Straight-path insertion and tile partition equality are tested. Curved paths use
local segment tangents; their join/texture quality remains a visual limitation.
Regular mathematical fibers are not yet proof of convincing natural dry brush.
The manual commands above must link every coverage implementation dispatched by CoverageRasterizer; the repository dependency guard checks these documented source lists alongside isolated build scripts.

### Dry-ink nearest-frame revision

Current renderer selects one closest centerline record per pixel, then evaluates
texture exactly once. It no longer takes a maximum of multiple segment texture
frames. A record contains four doubles (distance, pressure, signed normal,
arc-position), statically asserted32bytes/pixel. Dry-ink projects therefore
reserve46bytes/pixel tile scratch in MergedDocument instead of14; non-dry remains
14. The global128MiB memory and512Mi work limits are unchanged. Neighbor tangents
are interpolated for the normal sign; the normal magnitude is nearest distance.
Curved and straight tile-partition equality, straight split invariance, pressure,
seed repeatability, replay, cancellation, and memory-limit rejection were tested.

`normal-vs-dry-v1.png` and `DryInkCoverage-v1.cpp` in the local proof directory
preserve the earlier max-combined experiment. The current observed comparison
is `normal-vs-dry-v3.png`. **Still visually rejected:** mathematical fibers remain
comb/grid-like around curved sections. Removing multi-frame maximum alone did
not achieve natural dry brush; regular narrow fiber spacing and sampling remain
limitations. Do not use the technical test pass as artistic acceptance.

### Final limited dry-ink experiment (v4)

The current engine12 recipe replaces the earlier periodic cosine field entirely.
Bristles have seed-persistent nonuniform offsets and widths in normal coordinates.
Each bristle's ink supply is smoothstep interpolation of hashed48px arc-length
nodes, not random per-pixel particles. Five neighboring bristle intervals are
integrated against a1px normal-space box filter. Higher pressure increases width,
contact and deposited ink. Extra five candidate visits per resolved pixel are
included in both local32Mi work checks and MergedDocument's preflight work.
Nearest-frame32bytes/pixel memory remains accounted for; no global cap increased.

Old v3 source was preserved locally as `DryInkCoverage-v3.cpp`, alongside v3 PNG.
Final same-path comparison is `normal-vs-dry-v4.png`. This is the last scoped
recipe revision, not an open-ended promise of matching a human artist. Acceptance
must be based on this actual image, not merely the deterministic rendering tests.

Final visual review: v4 removes the conspicuous mesh/Moire pattern seen in v3
and produces continuous, irregular directional bristles with tapered pressure.
It remains an **experimental brush**, not artist-quality acceptance: medium/thin
strokes can resemble parallel wires and lack convincing bristle clumping and
broken ink edges. No full-cover redraw or natural-brush claim follows from this.
Strict dry-ink regression tests and MergedDocument budget tests passed after the
final recipe change. This scoped visual iteration is complete.
