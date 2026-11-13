# Restricted recorded closed-fill withdrawal

This is a safe-rework entry for new explicitly recorded closed ink, not arbitrary bucket deletion or automatic reconstruction. The desktop selects only the most recently recorded group, previews its complete removal and requires Accept or Cancel. The original line and independent later hand marks remain. Unknown later buckets, changed source context, split groups or recipe mismatch refuse without mutation.

Core admission does not trust a helper token or hashes alone. It reconstructs the creation event prefix, regenerates the bounded closed-ink producer with the recorded member IDs and compares every paint event and the full record. A single Withdraw command then atomically removes those members and that record. Other records must belong entirely to the prefix; later events must be validated independent native marks. Undo restores the full group. The changed target node is explicitly marked so tile caches invalidate, not merely Qt's full-frame display.

The pure raster-event validator was moved unchanged into a core inline header to allow command-boundary proof without a core-to-render library cycle; this is not a rendering-algorithm change.

## Limitations disclosed before acceptance
- Preview cancellation discards the result, but its private candidate apply also performs an uninterruptible core recheck, so cancellation is not always immediate. Final command admission rechecks synchronously and is not interruptible through the current API. An independent synthetic1725x2625 case (4,519,429 ROI pixels;72,310,864 coverage/edge work; no private reference) measured helper786–967ms and apply834–960ms across three runs. These exclude UI rendering/disk and do not establish universal responsiveness. A20ms cancellation request returned at67ms during helper validation, not during commit.
- Removing the final record returns to the older format generation. Saving over the prior format20 file is refused; explicitly save a new filename and retain the original. No silent downgrade or schema change is introduced.
- Lower-color removal can change translucent composite appearance while all other stored events are retained exactly. No claim of every final pixel unchanged.
- Artist-quality faithful artwork remains unfinished. This is an editor workflow improvement, not a new finished painting.

## Verification
Final local Release107/107 passed29.24s, including actual Qt/QML18.44s: latest-group preview/cancel/accept, unchanged original pending preview, exact reopened pixels, Undo/redo, absent-record refusal, cancellation and existing-format overwrite refusal with clean-copy save. Independent review found and fixed missing target-node cache invalidation; final review found no further blocking issue. Tests include exact original/unrelated event preservation, restored editing after withdrawal, Undo/reopen, counterfeit self-consistent metadata rejection, downstream unknown-fill refusal, mixed-command refusal, single-coverage-member groups, cancellation, tile invalidation and safe-copy format refusal. No Windows/physical-pen claim.
