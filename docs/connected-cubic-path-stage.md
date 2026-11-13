# Connected cubic paths: core stage (in progress)

## User purpose

Make editable contours useful for controlled, layered coloring. A two-sided irregular ink shape cannot be faithfully represented by simply replacing it with one uniform-width centerline. Independent cubic strokes also have separate endpoints: dragging one does not preserve its neighbor's join. This stage adds persistently shared anchors, not automatic image tracing or finished artwork.

Private reference review is local only. No private reference, crop, or generated reconstruction belongs in this repository. Synthetic fixtures demonstrate behavior, not artistic fidelity.

## Representation and boundaries

Engine 24 uses explicit open `3N+1` or closed `3N` controls, with 1–8 cubic segments. Adjacent spans share a single stored anchor. Handles are independent: there is no implicit smoothing, mirroring, snapping, or hidden topology conversion. Round static nib width is 0.5–128 pixels. Variable-width silhouettes and arbitrary negative-space reconstruction are not implemented by this representation.

A whole event is expanded into one engine-19 render surrogate, rather than compositing each span independently. Per-event work is bounded to 8,191 subdivision visits and 4,096 samples. Engine 23 and 24 share document-level limits, including 256 total spans; they do not each get a separate allowance. Existing engine-23 subdivision output must remain identical.

Creation and single-control editing are atomic commands. Explicit open/closed topology, all other controls, and event metadata remain unchanged during an edit. Existing recorded-fill and later raster-derived-event dependency protections apply. This is conservative rejection, not automatic fill reflow or fine-grained selective withdrawal. ClosedInkFill remains engine-19-only.

A geometrically closed contour is not a guarantee of a usable bucket-fill boundary. Low-opacity marks, intersections, image topology, layer selection, and rasterized gaps still matter. Nothing silently merges arbitrary layers or seals a gap.

## Format and data safety

The new native feature requires `DRAW-DELTA-24`. Protected save-as and journal/backup generation checks must prevent accidental overwrite or fallback that loses new events. Actual old-reader refusal must be checked using a genuine file emitted by the new writer, with file bytes unchanged. Existing 21/22/23 files remain supported; no implicit upgrade is requested.

## Acceptance status

This document records the completed local core-stage acceptance; remote CI and desktop 24 integration remain separate. B44–B50 focused tests have passed (7/7, 0.30 seconds) in the Linux cloud build. Production closed-contour fill interior/exterior pixels, unchanged original raster resources, uniform half-opacity composition, direct/tiled equivalence, allocation-triggered mid-call cancellation, cache recovery, and both directions of cross-generation journal/backup refusal are covered. The complete Release build passed and all 120 CTest tests passed in 87.58 seconds. All 10 independent native tool outputs were freshly linked; E02 ran 18 cases with zero failures. No QML Binding loop, ReferenceError, or TypeError diagnostics appeared in the full test log. The sealed release receipt will name the exact final tree and actual results. Older 23 test logs were not substituted for these runs.

This core stage does not yet expose connected-path editing in the desktop UI. The earlier four-control desktop stage is separate. Windows hardware, physical tablet input, full-size responsiveness, faithful artwork restoration, recovered source detail, CMYK, and physical print color are not claimed.
