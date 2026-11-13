# Persistent single-layer pixel strokes (format 25, WIP)

Goal: actual hand painting in one logical layer, with explicit per-stroke withdrawal/restoration after reopening. This is not an automatic reference redraw or a completed artwork. Reference images remain comparison-only.

One LayerNode stores an immutable ordered patch history and active cursor. Native RGBA patches preserve their coordinates; each stroke's opacity is composited inside the layer before overall layer opacity. This differs from separately compositing old independent layers: no automatic migration or hidden merge is performed. Rendering uses a prepared ROI index, not a worker rerun or per-tile PNG decoding. Shared patches are validated once per preparation.

`AppendRasterPatch` and `StepRasterHistory` are revision/source-manifest guarded atomic operations. New strokes while a redo suffix exists are refused; history is not silently discarded. Ordinary session Undo/Redo and persistent previous-stroke withdrawal/restoration are distinct operations. Reopening restores the persistent cursor, not the ordinary Undo stack.

A stage-bearing project requires `DRAW-DELTA-25` even with zero active strokes. Existing formats remain unchanged. Old readers must refuse this file, and cross-generation save/journal protections require a separate file rather than overwriting the original. Both active and redo records are validated. External worker results cannot add, remove or mutate this layer or its resource table.

Budgets cover unique resource Bytes ownership across current/Undo/Redo plus candidate copies, history map entries, record counts and prepared index/render work. A 128 MiB retained-resource limit is NOT a total-process RSS guarantee. No general garbage collector or unlimited painting is claimed. Withdrawn and old installed resources remain budgeted; reaching a budget refuses the operation without deleting history or modifying original artwork.

Local checks executed:
- Three core/storage/blend CTest targets passed (0.44 s): 40 synthetic patch strokes in one node, cursor operations and save/reopen, independent integer blend expectations, cross-tile equality and portable layer output.
- Admission CTest passed (12.95 s): shared versus separate Bytes ownership, map-entry budget, reused history manifests and external worker mutation rejection.
- The actual frozen old-format-24 portable reader refused real saved active and zero-cursor format-25 fixtures with `project.engine-version`. Input file hashes remained unchanged and no exports were created.

The integrated desktop checkpoint passed all six focused CTest targets (29.27 s):
- `Qt.MyPaintPersistent`: 33 actual pinned MyPaint worker strokes in one LayerNode, no source mutation while preparing, native layer/merged pixel equality, withdrawal followed by save/reopen and exact restoration, Undo/Redo, redo-tail refusal and cancellation.
- `Desktop.MyPaintPersistentUI`: actual offscreen QML mouse interaction for new paint layer, two accepted strokes in the same layer, persistent withdrawal/restoration across reopening, exports and cancellation. This is simulated input, not physical tablet validation.
- Four core/storage/blend/admission targets passed on the final integrated source, including dependency identity within the native patch hash. Identical pixels with distinct provenance cannot silently share an incompatible dependency table.

Desktop MyPaint remains optional and OFF by default. The worker returns an exact preview after release; this does not provide live MyPaint feedback while dragging. Existing independent raster layers are not silently migrated. The new persistent layer removes the previous per-stroke LayerNode/32-tag limit for this explicit path, not all resource limits. 33 small real strokes are a bounded test, not proof of unlimited full-page painting.

The final complete Release build and all 134 local CTest targets passed (131.76 s). The persistent QML interaction also passed five consecutive runs after bounded viewport synchronization; the original desktop MyPaint test passed separately. The stale busy wording was replaced with an accurate neutral handoff notice. GitHub CI remains unavailable because of the existing account billing/limit blocker; no retry, billing change or merge is part of this checkpoint. Windows, physical tablets, real-time MyPaint feedback, pigment mixing and artwork quality remain unverified. This is development capability, not delivery of a finished illustration.

## Remaining real-painting acceptance

After the final safety checkpoint, validate a small representative hand-drawn study rather than adding unrelated options: accurate proportions and contour, controlled pressure variation, dry/opaque stroke transitions, layered corrections, practical release/accept latency and exact save/reopen/export. A synthetic stroke fixture proves mechanics only. Reference images may be viewed for comparison, never silently copied into the final drawing. A poor study must be reported as such, not presented as a finished artist-quality work.
