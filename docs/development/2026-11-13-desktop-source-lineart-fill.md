> Historical first endpoint checkpoint. The 4Mi-pixel size restriction below is superseded by [native-size asynchronous fill](2026-11-13-large-lineart-fill-design.md); other unfulfilled art-quality limits remain.

# Desktop original-lineart fill checkpoint

## User goal, not completion claim
The desired output remains excellent, source-faithful, editable layered artwork. This checkpoint connects a missing desktop workflow rather than pretending another rough automatic redraw meets that goal.

## Implemented
- Import a same-size untagged grayscale PNG into a default empty desktop document. Preserve the exact encoded bytes and decoded RGBA in the project; locked Multiply original above a separate normal color-fill layer.
- One bounded immutable read feeds both decode and embedded source. No resizing, thresholding, tracing, color conversion or source replacement.
- A source-only bucket takes boundaries from the declared original RGBA, never previously painted composite colors. White/transparent seeds only; black/gray line seeds refused. Existing original pixels/resources are immutable.
- Mouse bucket uses inverse document transform and floor image coordinates; changing tools cancels an uncommitted stroke. Tablet input is deliberately not silently treated as brush strokes in bucket mode.
- Import is candidate-validated then one atomic application; Undo restores even the prior empty default hierarchy. Custom layer structures and existing work are refused, not silently removed. Fills are individual undoable events.
- Busy, replay and pending review block mutation. UI exposes exact failures rather than reporting success. Fill uses current color and opacity.

## Explicit limits
The synchronous first desktop path is limited to 4 Mi pixels and 4096 per axis, a strict original/fill two-leaf stack. It rejects larger input rather than downsampling. In particular **1725 x 2625 and 3300 x 5100 are not supported by this fill endpoint yet**. PNG images with a Qt-recognized valid color-space tag and profile-tagged documents are refused until proper conversion is available. Only grayscale source, not a colored finished illustration. No automatic gap closure, contour repair or antialias expansion is implied. Long synchronous operations remain a responsiveness limitation.

## Verification
Core B30 covers repeated fill geometry independent of existing color, separate closed regions, original resource/hash preservation, undo/redo/save-recovery, and invalid source/seed/budget/lock/cancellation cases. Actual Qt controller + software QML test covers original PNG import, fill, repeat, source preservation, undo back through import, nonempty/custom structure and oversized rejection, bucket mouse entry without a brush gesture, and QML control presence. Tests are technical fixtures, not private user artwork or an art-quality acceptance.

Linux Qt software only. Windows, physical pen input for the bucket, native OS file-picker automation, CMYK, professional painting quality and full poster workflow remain unverified/unimplemented. No private source image was uploaded.

## Remaining order
1. Budgeted/cancellable large-page original-lineart filling without downsampling, preserving transaction consistency.
2. Source-faithful editable contour repair and real-reference visual comparison rather than topology-changing simplification.
3. Natural stroke/material quality and a genuinely completed layered work, assessed visually rather than inferred from passing software tests.
