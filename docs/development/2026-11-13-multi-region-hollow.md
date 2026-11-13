# Independent multi-region hollowing and color

## Scope and design
Reuse the existing white-cover layer and upper color layer. Each additional selected original ink component contributes an atomic native white-cover Paint event; it does not create a new layer per click or replace source pixels. A shared semantic validator reconstructs a temporary source from immutable original plus every accepted cover. Covers must be full-white compatible bucket runs strictly inside original ink, mutually nonoverlapping. Existing color runs must stay inside their union. New hollowing rejects any overlap rather than wiping or reinterpreting previous work. Existing colors never enter flood-fill sampling.

This deliberately supports repeated **independent original ink components**, not an arbitrary lasso, automatic splitting of touching marks, unrestricted layer stacks, colored-reference interpretation, or manual brush edits in the controlled stack. One white overlay may contain multiple events. Opaque white paper is still required; this is not transparent erasure.

## Desktop acceptance plan
Actual Main.qml: create two broad separate source blocks plus a thin stroke; hollow/accept and color A; select B using the hollow tool; cancel without change; accept B without modifying A or creating extra layers; fill B a different color; check original resources, boundary pixels, Undo, save/reopen, repeat-click rejection and new review after reopening. Core tests independently check invalid overlapping covers, previous content retention and atomic undo.

## Evidence and remaining goal
Results will be recorded after execution. Technical fixtures and operations on old processed images are not new finished artwork. Faithful colored-reference interpretation, expressive brush/material quality, and an excellent completed layered work remain unresolved. No private artwork belongs in this commit. Linux Qt software tests cannot establish Windows or physical stylus acceptance. Continue reporting meaningful stages and blockers, not just implementation claims.

## Memory and review
The Qt selection RGBA buffer is released after flood selection and before command construction. The existing-cover validator's 4N temporary reference is released before new hollow erosion, retaining only the 1N cleared-domain mask. Incremental helper estimates are **not end-to-end RSS limits**: caller selection runs, source resources/history, temporary candidate validation, and display compositing remain outside them. Cancellation and original snapshot/revision checks are unchanged.

A separate code review found no blocking issue in the desktop dispatch/candidate path. The controlled stack is still deliberately restricted, and clicking a previously hollowed component refuses a duplicate cover instead of masking existing color.

## Executed focused checks
- Debug B33.HollowInkCommand + B34.DerivedHollowFill: 2/2 passed, 0.06 s after the memory release change.
- Final Release actual Main.qml/Qt software test: 1/1 passed, 16.33 s, including the full A-then-B authoring path above.
- These are focused regressions, not a claim of all-suite/Windows success.
