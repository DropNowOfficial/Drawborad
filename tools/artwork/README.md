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
  src/render/{ReferenceRenderer,CoverageRasterizer,FineLineCoverage,PixelBlend}.cpp \
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
