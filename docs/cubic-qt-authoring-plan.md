# Native cubic authoring: Qt integration plan (not implemented)

This is a read-only architecture assessment following the persistent engine 23
core stage. It is not a completed UI, an art-quality demonstration, a Windows
verification, or a promise of interactive full-poster rendering performance.

## Intended useful slice

Create one native curve; select an eligible existing curve; move its four authored
controls; inspect a real rendered preview; accept as one undoable command or
cancel without touching the original. Save/reopen must retain exactly four
controls. Never persist a guide polyline as a substitute for those controls.
The fixed round nib remains the current engine 23 contract; this does not add
variable-width calligraphy, tracing, or recover missing reference detail.

## Integration boundaries

Use `apps/studio/QtBridge` for document commands and preview transactions, and
`src/platform/CanvasItem` for native coordinates and exclusive pointer handling.
`WorkflowController` primarily owns source/segmentation/review processing; a
native cubic command should not become a segmentation or external-worker task.
The existing local-stroke selection is engine 19 sampled-point editing. Keep a
distinct eligible-cubic list, selected event ID and four-control model instead
of reinterpreting those sampled handles as Bezier handles.

Expose four fixed roles: start anchor, outgoing control, incoming control, end
anchor. Rebuild these from the selected persisted event after document changes
and reopening. Do not infer them from flattened renderer samples. Eligibility
must use the same core dependency and layer rules as the eventual command.

## Draft and preview state machine

1. Idle / selected: no uncommitted change. Choosing an eligible curve copies only
   its four controls and identity into a bounded draft.
2. Draft / dragging: keep four transient native-coordinate controls. Paint only
   a lightweight geometry guide and tangent lines; explicitly identify this as
   a guide, not final raster coverage. Do not render the whole document per move.
3. On the final pointer release (or an explicit Preview action), capture the
   immutable source snapshot, document ID, revision, canvas generation, selected
   event ID, active layer and draft generation. Queue at most one native preview.
   A superseding request cancels the old token and replaces it, not a backlog.
4. Worker: build the dedicated create or control-edit command, apply it only to
   a temporary Document, and validate with the actual native renderer. Reuse the
   bounded `LocalEditCompletion` mailbox/poll pattern and cancellation checks.
5. Main-thread delivery: require the same source identity, revision, canvas/draft
   generation and current token. Discard stale or cancelled results completely;
   neither pixels nor handles from them may replace a newer draft. Expose the
   exact preview snapshot through the existing display-snapshot path.
6. Accept: recheck identity, replay state, writable target and core dependencies,
   then apply exactly one command to the real document. Cancel: discard draft,
   preview and command with no original-layer change and no new undo entry.

Use a typed operation kind for preview status and acceptance messages. Existing
fill/local-sample wording and loose completion booleans are not suitable evidence
that a cubic was accepted. Render errors must remain visible; acceptance remains
disabled until the actual preview succeeds. A cancelled/failed preview must not
silently commit the guide or fall back to a lower-quality stroke.

## Busy gates and preventing data loss

Reuse current stroke-commit/fill-pool busy gates, pending command protection and
preview generation checks. Native freehand, bucket, hollowing, gap closure,
eraser and replay cannot mutate the document underneath a cubic transaction.
Lock controls during a pending preview, or explicitly cancel/supersede it before
allowing changes; never retain both an old pending command and new free handles.

Important additional gate: a Canvas draft *before* queueing a preview is currently
not represented by `fillBusy` or `fillPending`. Add an explicit draft-active state
to save, open, close, layer/tool switch and replacement guards. Require the user
to accept or discard it; otherwise an unsaved curve could vanish despite an
apparently clean document. Cancelling a temporary draft must not clear existing
project dirty state. Undo/redo or another document/layer/revision change must
invalidate the draft and selected handle safely rather than retarget the edit.

## Coordinates and pointer lifecycle

Use `CanvasItem::documentTransform` and its inverse, retaining native double
coordinates. Current `itemToDocument` intentionally rejects off-page positions;
it cannot directly serve tangent handles whose validated core range extends to
[-width, 2*width] and [-height, 2*height]. Add an explicitly bounded cubic inverse
mapping with finite/invertible checks. Initial anchors may require an on-page
click; existing valid off-page tangents must remain representable and editable.
Use a fixed logical-screen-pixel hit radius, not a document-pixel radius.

A minimal construction gesture can place/start-drag the first anchor/outgoing
control, then place/end-drag the last anchor/incoming control, followed by four
independently draggable handles. Document the incoming-control convention; do
not hide a mirrored or snapped coordinate. Allow exact numeric inspection.

Intercept this exclusive tool before freehand `beginSample`. Consume or clearly
reject tablet/eraser events rather than producing accidental freehand events.
Escape, focus loss, window deactivation, mouse ungrab, view resize and document
replacement must cancel the active drag safely. Pan/zoom/fit during a drag must
explicitly cancel the drag or use a frozen transform; changing the transform
mid-drag must never shift the stored control unintentionally.

## Acceptance evidence required before claiming the slice works

- Real Qt pointer create/drag/preview/accept produces one engine 23 event with
  exactly four controls; cancel and Escape produce none. No hidden freehand event.
- Select a reopened curve, edit any handle, observe changed native rendered
  pixels, then Undo/redo and save/reopen with exact authored-control preservation.
- Fit, 0.25x and 16x zoom plus pan on portrait 1725x2625 and poster 3300x5100
  canvases: accurate mapping, screen-space hit targets, bounded off-page tangents.
- Locked/hidden layer, fill-dependent curve and ambiguous subsequent operation
  refusal: no partial mutation of the source or unrelated layers.
- Stale identity/revision/generation, cancellation during preview, failure and
  draft-active save/open/close: no lost original work or resurrected stale preview.
- Real preview latency measured separately from lightweight handle-drag response;
  do not call large-canvas rendering fluid solely because guide motion is smooth.

Implement only after the current core tests and checkpoint are completed. This
plan changes no Main.qml, QtBridge or CanvasItem behavior by itself.
