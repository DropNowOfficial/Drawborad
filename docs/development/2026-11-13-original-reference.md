# Original-color reference attachment: separate from the artwork

## Goal / unmet artistic goal
The user wants faithful, layered artwork, from lineart to individually filled regions, not crude flattened recoloring. The available old thresholded mask has already discarded red accents, gray texture and faint hatching. Adding controls cannot recover that information. Preserve the original-color reference so these losses remain visible during work. This is not new artwork, restoration of unavailable detail, attribution proof, or an achievement of artist-level quality. Stage updates remain required; old processed pictures and new artworks must not be conflated.

## Bounded implementation
- Independent `attachOriginalReferenceUrl`, never the old external overlay importer that can rebuild the canvas.
- One embedded PNG/JPEG per project. Same bytes are idempotent; a different attachment is refused. Undo can remove a mistaken most-recent attachment. No automatic replacement or source deletion.
- Store exact encoded bytes plus versioned metadata/dependency as non-drawing Reference resources. No nodes, events, source raster or canvas dimensions change. EXIF/ICC remain in encoded bytes; metadata dimensions describe decoded oriented display.
- A separate read-only QML window displays the original beside the actual canvas. It is not an overlay, generated result or automatically registered comparison. Window fit is display scaling only; encoded pixels are never silently resized. PNG/ORA composites exclude this attachment; sharing `.draw` includes it, disclosed before selection and after attach.
- Limits: 32 MiB encoded, <=8192 each axis, <=20,000,000 pixels, <=256 MiB combined stored resource bytes (conservative counts include duplicates). These are not a process RSS bound; decoder, document history, rendering and color conversion add memory. Import/decode remains synchronous and may pause on large images; no fluidity promise.
- Display uses an immutable encoded-resource identity/hash/dimensions cache, not a per-stroke decode. Metadata is still checked on each changed resource map. First/new bytes undergo SHA-256 and decode validation. Hashes attest byte identity, not authorship or invisible-watermark absence.
- Known ICC is converted only in the decoded screen copy to sRGB; unrecognized/missing color space is explicitly unverified. Not CMYK, a printer profile conversion, or print-color validation.
- Busy/pending/replay refuse attach, exception/budget errors report failure, source is read once and length/error/EOF checked. Core helper stores bounded bytes; only the Qt entry point verifies actual PNG/JPEG decoding. Untrusted project attachment metadata/hash/dependency is checked before display.

## Acceptance
Tests added to actual `Desktop.QtSoftwareCanvas` executable: unequal attachment/canvas dimensions; unchanged nodes/events/spec and rendered frame; exact encoded bytes; same-byte no-op/different-byte refusal; Undo/redo/save/reopen after original source deletion; stale provider key; malformed dependency; dimension budgets; native 3300x5100 last-pixel preservation; real QML provider/window image Ready. All fixtures are synthetic and not user artwork. Final Release actual-QML test passed three consecutive runs: 16.58 / 16.02 / 16.91 seconds (49.50 seconds total). Cache-key preservation across AddLayer and Undo is also asserted. Parent 2d211f2 full local suite passed 103/103; its cloud and full Linux workflows both completed success before this publication. This revision has not yet run remote CI.

## Still missing
No semantic/color reconstruction from the original, no native recovered detail, no multi-reference replacement UI, no synchronized zoom/registration, no CMYK proofing. Windows desktop/physical pen remain untested. Continue comparing actual source details and avoid treating the reference itself as generated output.
