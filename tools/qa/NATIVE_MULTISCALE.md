# Native multiscale photo candidate (explicit opt-in)

`DrawboradHeadless run ... --planner multiscale` uses the existing reviewed
objects and native reconstruction transaction. `sweep` remains the default.
For the existing finite-job browser flow, set `DRAWBORAD_BATCH_PLANNER=multiscale`
on the gateway. Each new job persists its planner choice; restarting a worker
does not change it. Existing jobs without this field retain `sweep`.

The planner reserves capacity for corrections, chooses a bounded underpainting
nib, then traces local RGB structure-tensor tangents at widths 8, 4, 2 and 1.2.
Candidates are evaluated with native coverage and integer blending; only a
positive native squared-error improvement is retained. Temporary analysis and
feedback pixels are never document resources. Output remains ordinary marker
StrokeEvents, not vector-fill regions or baked source pixels.

The explicit opt-in permits 32,768 events / 1,000,000 samples per layer and
65,536 events per reconstruction transaction. Allocation exhaustion is not
convergence. Results remain `needs-review`. At the current 4 MiPixel product
gate, feedback is assembled from native 512-pixel render tiles in original
coordinates. This is NOT a 3300x5100 reconstruction implementation: the input
and memory gates remain unchanged, and no resampling bypass exists.

Supported: reviewed native binary masks and opaque RGB8 within each mask.
Fractional-alpha subjects are explicitly rejected by this candidate. Semantic
inference, hidden-surface reconstruction and physical material understanding
are not supplied by a structure tensor.

## Reproducing the diagnostic baseline

The production 8,192-event sweep rejects both full-size diagnostic photos.
`NativePlannerProbe.cpp` allows an explicit larger *diagnostic* budget to obtain
a genuine old-algorithm reference; this is not a claim the old production flow
completed. Build against the same native core/render/reconstruct libraries:

```
c++ -std=c++20 -O2 -Isrc tools/qa/NativePlannerProbe.cpp \
  -Wl,--start-group out/linux-headless/libdrawborad_reconstruct.a \
  out/linux-headless/libdrawborad_render.a out/linux-headless/libdrawborad_core.a \
  -Wl,--end-group -o native-planner-probe
./native-planner-probe source.rgba 451 300 sweep new-reference-directory 65536
```

Input is diagnostic, unprofiled RGBA8 with an explicitly whole-image mask; it is
not a substitute for the production manifest's profile/provenance validation.
The probe writes a native `.draw` and renders after clearing source bytes and
reopening it. Do not label its whole-image mask as semantic identification.

Photo sources: scikit-image v0.19.3 `chelsea.png` (451x300, Stefan van der Walt)
and `coffee.png` (600x400, Rachel Michetti), CC0, documented at
https://scikit-image.org/docs/0.26.x/api/skimage.data.html . No model ran.
