# Region review before committing color

## Why this instead of another automatic redraw
Audit of preserved contour studies found that closed hollow paths, SVG output, antialias coverage and native fill were already implemented. Rebuilding them would not solve the real failure: global treatment hollows tiny expressive strokes, classifies paper grain as islands and can select a two-pixel fragment instead of the intended ink region. A colored reference cannot safely be advertised as automatically recovered professional lineart.

## Implemented checkpoint
- Read-only bounded region review: actual selected area, bounding box, canvas-edge contact, explicit tiny-region warning, and a seed maximally distant from region exterior. Manhattan clearance measures pixel-centre distance, not a promised empty margin. No island deletion, contour smoothing or invented detail.
- Fill calculation creates a private candidate document. The desktop displays that actual candidate through the normal compositor, with an explicit uncommitted label; the committed snapshot and resources remain unchanged.
- User must accept the preview to make one undoable event or cancel it with no mutation. Source identity/revision/generation are rechecked before commit.
- Pending review blocks source changes, drawing, replay, open/new/save/export and close. Layer add/visibility bypasses were found in review and closed. Accept button waits for a rendered, non-error display. Waiting for review is not shown as a running worker spinner.
- Preview is separate from committed document; exports cannot silently omit or accidentally bake an unaccepted fill.

## Evidence and limits
Core B31 covers thin strips, holes, tiny islands, canvas edges, paired bucket run reconstruction, cancellation/budgets and both required native page sizes. Desktop checks pending source equality, preview/accepted pixel equality, cancel restoration, mutation/export refusal, acceptance followed by Undo/save/reopen.

A private existing hollow source was also exercised without regeneration: a previously reviewed meaningful region was selected, its area matched the old native-fill receipt, and the suggested seed moved toward the interior. The preview was inspected visually and then cancelled; the source remained unchanged. This is an old-source workflow demonstration, not a new complete artwork. No private image or identifying source data is included in GitHub.

Remaining: explicit selective solid/hollow/ignore authoring against the original colored source, preserving expressive fine marks, safe local closure repairs, and finished high-quality composition. The source still has inherited jaggedness and threshold loss. No claim of recovered native detail, generative reconstruction, artist equivalence, Windows acceptance, or complete PSD fidelity.
