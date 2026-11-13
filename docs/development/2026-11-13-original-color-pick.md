# Original-pixel color selection for manual lineart filling

## Specific fidelity gap
Old binary lineart cannot supply the reference's red accents, photo gray or paper tint. Arbitrary flat skin/hair colors are not faithful reconstruction. The next usable step is explicit original-pixel color selection, not automatic recoloring, invented texture, or filling an intentionally blank figure. The private source was inspected locally only; no original/processed art is in this change.

## Behavior
- Click in the separate embedded-original window to preselect one decoded native pixel. Mapping uses displayed image bounds, excludes letterboxing, floors native coordinates, and rejects nonfinite/out-of-range coordinates. Never samples a screen screenshot or its interpolated color.
- Bridge checks the expected attachment URL against current resource identity before returning RGB, source alpha and pixel coordinates. The screen copy applies recognized ICC conversion to sRGB; absent profiles remain explicitly uncalibrated.
- Click alone changes neither tool nor document. A separately labeled button applies RGB to current brush/fill, preserving tool opacity/width/hardness/taper. Original alpha is displayed, not silently multiplied/copied; fully transparent pixels cannot be applied. Half-transparent pixel RGB is not its composited visible appearance. Resulting paint depends on opacity, existing pixels and blending, so same visual color is not promised.
- Changing the project clears preselection conservatively, even for unrelated edits. Existing strokes are not changed; current gesture settings remain frozen. No image generation, automatic painting or private upload.

## Acceptance
Actual Release Qt/QML test passed three consecutive runs: 16.22 / 15.77 / 16.27 seconds. Tests cover native pixel/fractional coordinates, stale identity, negative/right/bottom/nonfinite bounds, separate RGB/alpha, unchanged snapshots, real mouse selection, explicit application retaining opacity, resize mapping, letterbox rejection and attachment Undo clearing selection. Existing native poster/fill/gesture tests also run in that executable.
Private original-reference read-only sampling was run using environment-provided input and points and compared with an independent QImageReader decode. It made no drawing or external artifact and did not upload the source. A sampled pixel is not a regional palette average or recovered brush texture.

## Remaining goal
This closes a manual color-selection obstacle only. Semantic shape reconstruction, varied natural marks, faithful texture, deliberate lettering and complete artist-quality artwork are not delivered by this feature. Source resolution is unchanged; no CMYK or Windows/physical-pen acceptance claim. Continue stage reporting and distinguish technical tests, old-source operations and new artwork.
