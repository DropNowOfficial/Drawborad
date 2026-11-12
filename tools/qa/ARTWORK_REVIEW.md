# Original-size artwork review

PR #8 records real artistic failures despite software tests. This tool makes local loss visible without treating numeric similarity as artistic acceptance.

```
python tools/qa/artwork_review.py --source source.png --render native-final.png --regions regions.json --output review-new
```

`regions.json` is a JSON list, e.g. `[{"name":"face","box":[20,20,90,110]}]`. Coordinates are EXIF-oriented source pixels, x0/y0 inclusive and x1/y1 exclusive. Source/render must have identical dimensions. Use a render containing the intended same objects; a deliberately hidden object is a meaningful difference, not automatically a reconstruction failure.

Open `review-new/index.html`. Inspect the source, candidate and unamplified absolute difference at 100%; 200% changes display only. Horizontal/vertical scrolling preserves actual pixel dimensions. The standalone HTML embeds both images, has no remote assets, and must remain private for private inputs. JSON records input SHA256, coordinates, original dimensions, whole-image and region measurements. Prior output directories cannot be overwritten.

Metrics are encoded 8-bit RGB diagnostics: worst channel error over black/white composites, alpha error, darkening/brightening on white, and adjacent-pixel edge changes. They expose blur, contour displacement and tonal shifts but do not identify their cause or provide perceptual color accuracy. Hidden RGB does not count as visible error. ICC differences are flagged, not silently converted. Untagged inputs are explicitly untagged, not verified sRGB. An ICC mismatch means color interpretation requires separate review.

No automatic artistic acceptance, native editability certification, restored-detail claim, print-size approval or new drawing generation. The input can be any rendered candidate, so native provenance must be established separately using C++ replay/export evidence. Current cap: 4 Mi pixels, 8-bit RGB/RGBA/L/LA, 32 named regions; larger print-size documents and other color/depth formats are rejected. Human review and the existing native QualityEvaluator remain authoritative for their respective gates.
