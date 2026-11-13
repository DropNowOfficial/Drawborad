# Native-size desktop source fill: bounded asynchronous design

Goal: support 1725x2625 and 3300x5100 source lineart directly. No resizing or fabricated detail.

- Match shared display budget: <=20M pixels, <=8192/axis, existing source resource cap unchanged. Source RGBA is immutable shared snapshot memory. Helper temporary reference + RGBA + visited is approximately 9 bytes/pixel plus bounded runs/events; snapshot, display and allocator costs are additional, not a total-RSS guarantee.
- One private QtBridge worker, no queue/new task while busy. Capture immutable source snapshot, active target, color and document generation. Build only a Command off GUI thread; never mutate document or QObject there.
- GUI completion commits once only when not cancelled and source document/revision/generation still matches. Cancellation/failure discards command and leaves source/Undo unchanged. Cancel is cooperative, not guaranteed instantaneous.
- Busy prevents drawing/undo/new/open/save conflicts. Save during pending fill is refused explicitly rather than falsely claiming the pending fill was saved. Existing committed state remains safe; after success save includes fill.
- Destructor requests stop and waits before releasing worker-owned lifetime. UI shows preparing/working/cancelling/completed/error, with cancel button.
- Embedded PNG import remains a synchronous bounded operation in this increment, now at native dimensions. This is an acknowledged responsiveness gap, not hidden under the fill-worker claim.
- Verify original-size source, source hashes, event bounds, repeated fill, cancellation and stale-source guards, GUI heartbeat, undo/save/reopen. Linux software only, not Windows/physical pen/art acceptance.

## Implemented and checked
- Core now allows native dimensions up to 20M/8192, with an incremental scratch estimate capped at 256MiB (9N plus bounded run/event workspace; excludes original resources, history, validation/application and display).
- Reference copy checks cancellation per 64KiB; bucket RGBA conversion per 16384 pixels. Existing 65536 run / document event limits remain unchanged.
- Desktop single job, explicit pending/cancelled/completed notices and cancel control. Replay switching is refused while pending; completion checks replay too. Window-close is blocked until pending fill finishes or is cancelled, including saved-clean documents.
- Core native-size fixtures cover both required sizes, exact full interior region, source resource/hash preservation and large save/recovery. Under-ink regression still passes.
- Actual Qt software test imports both native-size grayscale PNGs, starts background work with event-loop heartbeats, cancels after event-loop yield with no source change, refuses save while pending, disallows a second job, completes original-size color geometry, verifies original bytes, saves, undoes and reopens exactly.
- Final Release Qt software regression passed (16.00s). Debug core and software regression separately recorded in the publish receipt.

These are native-size technical fixtures, not a claim that a private colored reference has become usable lineart or that the user's artwork is complete. The private recovered reference remains colored and is correctly refused by this grayscale-specific endpoint. Source-faithful contour extraction/repair and professional-quality layered artwork still require work. No private artwork was published.

Known limits: import/decode, final command application/validation and file I/O are still synchronous. Cooperative cancellation may wait for resource validation/allocation. Extreme process-OOM recovery and hard process-RSS limits are not guaranteed. Windows and physical input remain unverified.
