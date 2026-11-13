# Canvas drag selection for bounded hollowing

The numeric rectangle now has a mouse drag counterpart. This is deliberately two-step: **drag sets only the range; a subsequent click on original ink inside it requests the existing actual hollow candidate**. Dragging never commits paint. The candidate still requires accept/cancel, and an accepted cover is undoable.

Both endpoints map through the current document transform. Reverse drags are supported. Pointer coordinates snap outward to integer source pixels (floor minimum, ceil maximum), explicitly distinct from the numeric input path, which refuses fractional values. The visible yellow guide follows the same document rectangle used by the viewport. Numeric fields synchronize on a completed drag, retaining precise manual adjustment.

A drag cannot begin during render/busy/replay/error states. Out-of-canvas movement or release, zero-size gesture, Esc, focus loss, lost capture, source/view changes and resizing cancel without replacing the previous range or editing the document. The tool switches back to ordinary bounded ink selection after completion. Bounds remain transient tool state, not persisted document content.

## Verification and limits
Actual Main.qml regression exercises the drag button, reverse drag after zoom/pan, exact snapped source rectangle, unchanged committed source during drag, numeric synchronization, Esc/outside/view-change cancellation, then actual candidate pixels, cancel/accept and Undo. Results are added after execution.

Still rectangular, not freehand lasso or semantic artwork interpretation. It does not make the old processed image a new work or meet the complete high-quality layered-art goal. Windows and physical pointer hardware remain unverified. No private artwork is uploaded.

Final Release actual Qt/QML focused regression passed (1/1, 16.18 s), including reverse zoom/pan selection, zero-size/outside-start/outside-move/Esc/focus/view cancellation, untouched source while selecting, real candidate pixels, cancel/accept and Undo. Read-only review checked signal ordering so restoring the numeric binding cannot overwrite a newly dragged range. Esc cancels the current gesture and permits retry; the explicit button exits selection mode. These are focused automated Linux software checks, not Windows/hardware or complete-art acceptance.
