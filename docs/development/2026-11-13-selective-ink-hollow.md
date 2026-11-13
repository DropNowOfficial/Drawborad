# Selective ink hollowing core, not another whole-page redraw

## User direction and communication
Preserve the original width and outer shape of selected ink, hollow its interior for coloring, and retain fine expressive marks. Do not skeletonize thick marks into centre lines or apply global edge extraction to the entire page. Report usable stages and real blockers promptly. Any artwork delivery must distinguish old processed sources, new operations on those sources, and genuinely new completed artwork; uncertain source provenance must remain explicit.

## Implemented core
`hollowInkRegion` takes an explicit binary ink mask and selected scanline runs. It only clears pixels whose full radius-1 or radius-2 square neighborhood belongs to both ink and the selection. This conservative intersection preserves original outer/inner boundaries and the selection's own cut edge; it never expands the selection. One/two-pixel thin marks survive radius1; up to four-pixel thin marks survive radius2. No small islands are silently deleted. Empty interior is legitimate and is not an excuse to expand the selection.

20M pixels / 8192 axis; two byte-per-pixel scratch arrays, bounded output runs, row-wise cancellation. Original mask input is never mutated. This is pixel-grid geometry, not invented subpixel/native detail.

`hollowInkOverlayCommand` applies the derived interior as one editable **opaque white paper overlay** above a locked grayscale lineart source. Original resources remain untouched; one command creates the layer and white bucket event, enabling Undo and layer hiding. It explicitly requires opaque white paper and a top-level last source layer. It is **not transparency erasure**, arbitrary colored-image segmentation, repeated unrestricted-stack processing, or a newly wired desktop tool. Further authoring/UI integration is still needed.

## Validation
- B32 checks broad shapes, holes, concavities, diagonal contact, partial selections, thin marks and islands against a per-pixel neighborhood oracle, plus malformed inputs, budgets and cancellation.
- B33 checks native command application, exact unchanged pixels outside the safe interior, original resource identity/hash, one-step Undo/redo, layer hiding, white-paper restriction and save/recovery.
- Actual existing reference-derived dark mask was used at its own native dimensions, without resizing. A selected meaningful ink region was locally hollowed, independently compared with a separate erosion implementation, visually inspected, undone/redone and save-reopened. No difference outside the permitted interior was found. This is a new local operation on an old processed source, not a new completed painting. The old mask already excludes some colored content and contains threshold losses; this operation does not recover those details.

Only generic source/tests/notes are published. Private before/after images and native project remain local, not in GitHub. Windows, full original colored-source fidelity, interactive authoring integration, professional brush quality and complete finished artwork remain unverified/unmet.
