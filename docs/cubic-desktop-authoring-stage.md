# Cubic desktop authoring: verified Linux Qt checkpoint

This separate source tree starts from the 717-file preserved core manifest. It
must not replace the core checkpoint until its own tests and manifest comparison
are complete. No GitHub upload is implied by this document.

## Implemented and validated path

The Qt bridge keeps four native-coordinate draft controls and the immutable
source snapshot. A new curve starts as a clearly labelled editable guide, not an
automatically generated artwork. The selected native layer must be writable.
Existing eligible engine 23 events can be selected from their persisted IDs.
Their four authored controls are recovered directly, not reconstructed from the
renderer samples. The candidate menu is cached by document/content identity and
active layer; rebuilding it can still be costly on large histories.

MouseArea dragging uses a bounded inverse of the canvas transform. During drag,
only the four-point geometric guide moves. On release a one-worker transaction
constructs the real command, applies it to a temporary document and validates the
actual native merged render. Source/revision/generation and stop-token checks
protect acceptance. This is not per-frame full-document rendering.

Explicit accept adds one undoable command. Return-to-handles discards only the
pending rendered preview and retains the draft; abandon-draft discards both.
Save/open/close and conflicting operations are guarded until the draft has been
accepted or explicitly abandoned. Draft interaction locks are distinct from the
visible progress indicator: a stationary draft is not a running computation.

The current fixed round width/color contract remains that of engine 23. The
first version starts a four-control template; it does not implement freehand
curve fitting or a multi-segment Pen tool. Full native preview can take time at
poster sizes. The control overlay is explicitly a guide, not final coverage.

## Verification

A clean rebuild of every Qt target object removed an earlier stale-object layout
mismatch caused by editing a header while a test translation unit was compiling.
The warning-as-error build then passed, followed by the complete single-thread
Release/HEADLESS/QT build and **117/117 CTest**, total **88.04 s**:

- Existing `Desktop.QtSoftwareCanvas`: 21.27 s.
- `Desktop.CubicBridge`: 0.22 s. Create/edit, real preview, two-step retained-draft
  iteration, accept/cancel, Undo/redo, saved project reopening and continued edit,
  draft save refusal, cancellation followed by a new draft without stale commit.
- `Desktop.CubicControls`: 2.31 s. Actual QML MouseArea release preview, explicit
  two-case zoom/pan inverse mapping, real zoomed/panned drag, out-of-bounds release,
  mouse ungrab, overlay hiding, resize and genuine focus loss to a second active
  QQuickWindow. Each interrupted drag restores its prior controls. Draft close
  and export are blocked, and a non-working draft does not display a spinner.
  The curve panel has its own visible accept button, exercised by actual mouse
  click after scrolling, rather than requiring a distant generic accept control.

The actual portrait path exports 1725x2625 RGB PNG and ORA. Independent archive
inspection checks native dimensions, a nonempty Paint layer plus Paper layer,
and exact equality of all RGBA bytes between PNG and ORA mergedimage. Export is
not a test that missing source detail was restored or print colors were matched.

An extra export run exposed a visibility binding loop after the first passing
suite. It was fixed by a dedicated four-control notification that only refreshes
handles, not general bridge state or full rendering. The final controls test
asserts zero Binding loop/ReferenceError/TypeError messages; the complete final
LastTest log also has zero matching diagnostics.

The final source fingerprint was unchanged throughout the full test run. This
stage document alone was then updated with the completed results. A separate
helper loads this same actual Main.qml and takes QQuickWindow::grabWindow for a
synthetic single-curve technical demonstration; it does not generate artwork or
load any private reference.

No Windows, physical pen, full-budget fluidity, artistic fidelity, connected-path
editing or finished-artwork claim is made. The new UI path was specifically
exercised at portrait size; these tests do not establish poster-scale curve
interaction latency. Initial menu construction still rechecks individual curve
eligibility and can be costly on a full history; only unchanged-menu reuse is
cached. Full native preview is explicitly separate from lightweight guide motion.

## Explicit scope boundary

This is one fixed-round-nib cubic segment with four controls. It is not a
connected compound path, variable-width outline, automatic contour reconstruction
or a complete illustration. ClosedInkFill still accepts only its engine 19
smooth-ink outline contract; this change does not make engine 23 eligible for that
closed-ink command. The separate selected-boundary fill path has not been
specifically qualified with these new curves and is not claimed here.
