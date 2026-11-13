# MyPaint mouse pressure taper experiment

## Goal and limits
Fixed mouse pressure produces mechanical endpoints. This opt-in linear arc-length envelope offers start/end pressure variation to the actual pinned MyPaint brush. It does not create painting detail, restore reference resolution, or establish artwork quality. Default is off; real pen pressure is unchanged. No new native format or raster flattening.

## Implementation
Mouse pressure, device identity and taper are frozen at pointerdown. Keep original samples; add at most two envelope ramp knots, interpolating geometry, pressure and floating time. Multiply pressure by an envelope with a 5% floor. If output exceeds 1024 samples, cancel the whole stroke, not truncate. Zero-length input remains unchanged. The existing guarded worker receives standard points and persists exact transformed global samples in its manifest. No extra worker API fields or guessed brush preview. Existing blue path remains.

The envelope is linear, not smooth. MyPaint pressure affects width/coverage/texture. Added samples can change integration time for equal timestamps, so sampling-density invariance is not claimed. Very short pointy-ink strokes still reject for no visible ink. Recovery snapshots remain subject to existing workspace lifetime limits.

## Evidence
Actual helper/pointer tests cover freeze-on-down, real pen unchanged, two-point ramp knots, time order, immutable input and point-budget rejection. Real pinned worker tests cover both presets with dense, two-point, same-time and 1-pixel-short strokes: 8 passed. The short pointy-ink case rejects both fixed and tapered input, without a completed manifest.

The local technical comparison renders two presets at 0.15/0.35/0.7 pressure, fixed versus 20% taper (12 samples). Dry-brush endings fade visibly, low/medium pointy ink narrows at endpoints. High-pressure pointy ink retains mechanical central shape and dab scalloping; this is NOT a fix for natural painterly forms. Source-only reproducible study script is provided; private artwork is not included. Browser acceptance is extended to operate the selector, inspect actual posted pressure and retain undo/save/reopen checks; remote run is still pending.

Full local regression: 85 Python tests + 23 subtests passed; all socket-free UI scripts passed. Independent code and visual reviewer found no blocker for opt-in experimental release, explicitly rejected high-pressure pointy ink as natural-brush quality. Native code is unchanged from the folder build.
