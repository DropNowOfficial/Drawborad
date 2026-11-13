# Uncommitted live stroke preview (bounded WIP)

Preview is rendered from an immutable committed snapshot plus a temporary native
Paint command in a private in-memory Document. It uses the same brush, opacity,
pressure, grain seed, masks, eraser and compositor as commit. It is not a solid line
painted over the page, and no preview is written into the source document or files.
Gesture settings and random grain seed are stable from pen-down through release.

Requests are coalesced on a 33ms timer, not a 30fps guarantee. One running worker is
allowed to finish while only the newest queued request remains, preventing starvation
under continuous input. Epochs invalidate source changes/commit/cancel; sequence
checks and one completion mailbox prevent old callbacks from replacing newer frames.
The committed frame is retained for restoration. Full-canvas copies/compositing can
still lag on large documents; multiple cached frames exceed the compositor's own
128MiB work-budget accounting. No end-to-end memory/FPS acceptance is claimed.

UI explicitly marks the picture as uncommitted preview. Release commits once; Esc,
lost capture/focus, view changes, replay/source changes or busy-state transitions
cancel. Failed compile/commit reports that the stroke was not saved, rather than
silently disappearing. Allocation failure in preview retains committed source state.

Validation: actual software/QML window, translucent mouse+taper preview versus commit
pixel equality, unchanged source during preview, undo and Esc restoring original
pixels; simulated pressure-pen/eraser previews and eraser-to-commit parity; invalid
timestamps fail without changing the source or reviving an old preview; poster preview
status and preview-to-commit pixel parity at3300x5100; existing saved/exported data checks.
These are synthetic input cases, not physical pen/Windows acceptance or proof of
natural professional art quality. Source-faithful contour repair and artistic finish
remain separate, unfinished goals.

Unsupported composite layouts report preview unavailability instead of approximating
with a misleading opaque overlay. Legacy transformed-native display support does not
imply that every such document can be preview-composited by this bounded path.
