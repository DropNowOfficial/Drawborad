# Bounded selection inside connected ink

## Why this increment
A connected ink component can contain several intentional marks. Whole-component hollowing is too coarse, while automatic interpretation of a colored painting cannot promise source-faithful decisions. This increment therefore adds explicit user-controlled rectangular selection in original pixel coordinates, not automatic redraw or color extraction.

The desktop offers an opt-in X/Y/width/height restriction with a visible yellow range box. Click original ink within the box. The original component is flood-selected, intersected with the exact rectangle, then the existing safe-interior hollow operation runs on that intersection. Its erosion preserves both original ink boundaries and the rectangle's cut boundary. No centerline conversion, enlargement, smoothing, or tiny-island deletion occurs.

The yellow box is a selection guide, not a committed result. The actual altered pixels still use the normal candidate display and accept/cancel transaction. Original resources stay unchanged; an accepted operation is an undoable white-cover event. The existing derived bucket can color the resulting enclosed interior. Other content, saved-source identity, and compatible earlier covers remain protected.

## Safety and scope
Coordinates must be finite nonnegative integers; dimensions positive, wholly inside the canvas, with the clicked seed inside the half-open rectangle. Invalid or fractional input refuses instead of silently rounding/clamping. Core clipping validates every original run and count, including runs outside the rectangle; cancellation and bounded output runs are supported. Settings are frozen into the asynchronous request. Controls cannot change the range while busy/reviewing.

This is numeric rectangular restriction, **not drag selection, freehand lasso, semantic object selection or full colored-source restoration**. A rectangle crossing a single connected component may include multiple disconnected subparts. Previously cleared overlap is still refused. New document/reopen keeps normal pixel-space validation; no selection is silently persisted as source data. The rectangle is current tool state: reopening within the same window can retain the controls, but a `.draw` file does not store them.

## Verification
B35 checks exact intersection, connected wide-ink partial erosion and preserved cut edge/right-side pixels, fine selection, invalid/overflowing rectangles, bad input runs/counts, empty overlap and cancellation. Actual Qt/QML regression exercises the numeric fields and visible guide, source-only candidate, accept/cancel, partial block edge pixels, Undo/save/reopen and subsequent interior color. Executed results recorded after tests.

No private artwork is uploaded. Technical fixtures are not newly completed artwork, and these controls do not satisfy the outstanding high-quality complete-work goal. Windows/physical input remain unverified.

## Executed checks
- Debug B35.ClipFillSelection: 1/1 passed.
- Final Release actual Main.qml/Qt software regression: 1/1 passed, 16.73 s, including empty field/NaN/negative or zero size/fractional/outside-seed refusal, actual bounded preview, cancellation, acceptance, Undo, save/reopen and independent interior fill.
- Separate read-only review found no blocking issue. These focused checks do not imply all-suite or Windows acceptance.
