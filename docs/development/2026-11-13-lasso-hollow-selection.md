# Controlled lasso for source-selected hollowing

## Reference-driven reason
An existing private source-derived cover contains a tilted photograph surrounded by other ink. An axis-aligned rectangle cannot follow its slanted edge: it either misses corners or includes neighboring ink. This motivates polygon selection, not automatic reinterpretation or a new drawing. The old mask already lacks some source color/faint detail; clipping cannot recover that information.

## Implemented contract
A freehand mouse drag records at most 512 source-coordinate vertices, with consecutive samples at least one source pixel apart. Release implicitly closes the polygon. An endpoint within one pixel of the first is treated as closing rather than a duplicate vertex. Self-intersection/self-contact, degenerate area, overlapping reversed edges and out-of-canvas input refuse without replacing the prior selection. The core independently validates all vertices and all original runs.

Rasterization uses pixel centers, an even-odd rule and a half-open crossing convention, sorting row intersections and intersecting only original component runs. No whole-image reconstruction, resampling, smoothing or tiny-island removal is performed. The existing safe-interior operation protects polygon cut edges as well as original ink edges.

Dragging selects only a guide. A subsequent click inside the polygon and original ink requests the normal candidate. An outside seed refuses, even if it belongs to the same original ink component. Acceptance, cancellation, Undo and native save/reopen use existing cover events; source resources stay immutable. Reopened fill reconstructs from accepted runs, not transient lasso state. Numeric rectangle editing clears the polygon and intentionally returns to rectangular restriction.

## Limits and evidence
The freehand path is a bounded sampled polygon, not a semantic outline recognizer or curve-perfect vector lasso. Straight closing edges are explicit. Some intricate paths exceed the vertex budget or self-intersect and must be retried rather than silently repaired. Transient selection guides are not document content.

B36 validates slanted convex geometry against independent pixel-center half-plane membership, reverse winding, concave paths, sparse masks, half-open boundaries, invalid paths, cancellation and safe hollow edges. Actual QML exercise and private old-source comparison results are recorded after execution. No private source/output is committed. Neither this workflow nor the old material demonstration constitutes a new finished work, faithful colored-source restoration, artist-level completion, or Windows/physical-device acceptance.

## Private existing-source operation (not new artwork)
A locally retained old binary source mask was processed at its unchanged 1024×1536 dimensions with a slanted polygon. It selected 41,974 original ink pixels, cleared 32,038 safe-interior pixels and retained 9,936 selected boundary/detail pixels. Independent Python half-plane membership plus 3×3 erosion matched every changed pixel; all outside pixels were identical. Compared with the polygon's bounding rectangle, 1,774 additional pixels were protected from unwanted hollowing. Source resource equality, Undo/redo and native save/reopen were checked. The local image was visually inspected: the operation follows the tilted photo region, but it does not restore lost source colors or details and is not a completed illustration.

Initial Qt regression encountered one failure in the pre-existing poster live-preview screenshot equality assertion, before new lasso tests. An unchanged retry passed (18.15 s). This observation is retained rather than presenting the first run as clean; repeated final regression results follow separately.

Final Release actual Main.qml regression passed three consecutive repetitions (19.06 s, 18.23 s, 16.51 s). It covers valid freehand closure and shape preservation, prior polygon retained after invalid path and Esc (not merely its bounding box), outside-polygon seed rejection, candidate pixels, accept/Undo/save/reopen and subsequent interior color. Earlier sporadic poster preview screenshot failure is still noted above; three passes are not proof that unrelated race cannot recur.
