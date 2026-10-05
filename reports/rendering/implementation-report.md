# R01-R06 rendering implementation report

Scope: canvas, brush and replay batch only. Base: foundation commit `92aef36bc8c058f6ecb3432030fe2faf9d5505ae`.

## Implemented

- R01 deterministic CPU reference renderer: 512x512 indexed tiles, fixed raster coverage, RGB/CMYKA 8/16-bit raw pixels, eraser and canonical pixel hashes. Imported/source-linked pixels are rejected instead of used as a hidden fallback.
- R02 incremental tile and display caches: local dirty-tile calculation, pinned LRU content cache and profile-keyed display cache. A Qt scene-graph CanvasItem is included for target-machine compilation; it renders indexed CPU tiles as native Qt textures and accepts mouse/tablet input.
- R03 deterministic brush compiler: pressure preserves a real zero, missing tablet values are not invented, resampling is distance based, eraser is explicit, grain is seed-derived.
- R04 indexed replay: 256-event chunks, direct seek, forward/backward step, and new-marks event selection without changing the document.
- R05 viewport/hit coordinates: screen/document transforms, stable zoom anchor, DPI separated from document pixels, hidden/locked nodes are not paintable.
- R06 resource/device recovery core: disposable-cache eviction, CPU fallback decision, display-cache reset on graphics-device loss, synthetic benchmark CLI.

## Fresh verification in this environment

`lab-debug`: 16 CTest entries, 14 passed, 0 failed, 2 Windows-only foundation tests skipped. R01-R06 all passed.

`lab-release`: 16 entries, 14 passed, 0 failed, 2 Windows-only foundation tests skipped. R01-R06 all passed.

`lab-asan`: 16 entries, 14 passed, 0 failed, 2 Windows-only foundation tests skipped. R01-R06 all passed with ASan/UBSan enabled.

## Not claimed

The new rendering batch has not yet been compiled on the user's Windows/Qt machine. The Qt CanvasItem source is therefore NotRun. Physical tablet pressure/tilt/eraser input is not qualified. New-renderer RTX 4070 timing and real device-loss recovery are not measured. ICC display conversion and advanced blend modes remain in the color/file batch. The benchmark CLI measures only its named cache scenario and is not presented as GPU frame-rate evidence.
