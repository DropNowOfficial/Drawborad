# Explicit visible short-gap closure

## Intent, not automatic contour guessing
The user chooses two original-ink endpoints by dragging. Release requests a candidate; it does not commit. The candidate is a visible black one-source-pixel Bresenham line, at most 64 pixels including endpoints. Both endpoints must be original grayscale ink, and the path must cross at least one nontransparent original-white pixel. It cannot cross an accepted hollow interior. Invalid/long/duplicate/unsupported edits refuse instead of extending or inventing a contour.

The yellow drag guide is only an endpoint guide, not the final raster line. Actual candidate pixels are shown before accept/cancel. The original raster/resource is immutable. Corrections are native events in the independent visible correction layer (shared with accepted white hollow corrections); the layer can be hidden and an individual last operation undone. Hiding that layer hides all its corrections, not only the newest line. No claim of a separate layer per gap is made.

## Sampling and persistence
The controlled correction layer accepts only validated full-white hollow runs or full-black short straight barriers. Black geometry is reconstructed and compared with canonical Bresenham pixels, not accepted merely because it is labeled a repair. A correction-only stack without hollow events is supported. Original-white fill samples original pixels plus accepted black barriers; upper interior fill samples original plus white covers plus barriers. Previous colored output is never sampled or changed. Existing lower paint can continue underneath a new visible barrier; subsequent fill respects it.

Native events persist across save/reopen. Atomic creation/append, candidate cancellation, Undo, source equality, layer-hide behavior and subsequent fill are tested. Work remains bounded by existing canvas/event budgets; these do not imply a global memory bound.

## Verification and artistic boundary
B37 verifies an open synthetic region first leaks, the explicit accepted barrier separates it, later interior fill leaves outside colors unchanged, and ordinary/hollow workflows still combine safely. It checks illegal geometry, anchors, length, overlap, cancellation and native recovery. Actual Main.qml verifies the drag tool/guide, invalid white anchors, candidate/cancel/accept, hiding correction/Undo, interior-vs-exterior pixels and reopen.

On real old processed material, an open mouth mark may be an intentional stylistic omission. A local explicitly chosen endpoint trial may test the function there, but **must not be described as fixing an artistic mistake or an approved correction**. The user retains that decision in preview. No private images are uploaded and no new complete artwork, recovered detail or Windows/physical-device acceptance is claimed.

## Executed evidence
- Debug B37/B34/B33 core: 3/3 passed, 0.20 s.
- Actual Main.qml Release path passed (17.23 s), including invalid anchors, drag guide, real candidate, accept/cancel, layer hiding, Undo, no outside leak and native reopen. Final wording explicitly states that ordinary fill samples original plus accepted barriers.
- Private old-source optional trial: a 27-pixel explicit line added 23 new black pixels; the selected white component changed from 26,907 to 1,066 pixels. Independent Bresenham and connected-component checks matched every changed pixel. Actual controller candidate/cancel/source equality, Undo and reopen passed. The open mark may be intentional: this is a reversible feature trial, not an approved artistic edit or finished work. Private output remains local.

Final Debug core+actual Qt: 4/4 passed, 74.92 s. Final Release Qt after wording correction: 1/1 passed, 17.56 s.
