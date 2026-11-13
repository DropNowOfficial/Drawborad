# Stroke preview readiness follows received input

## Concrete state gap
The preview timer waits 33 ms before scheduling a render. Previously, `appendSample` did not mark the new input pending; `previewing` represented only an already scheduled worker. An older completed worker could therefore report ready while newer received samples still waited in the timer. A test or UI could observe a previous frame as if it covered the latest input.

This is a real readiness-state defect found by source inspection. It is **not proof that it was the only cause of the previously observed poster screenshot mismatch**.

## Fix
Every accepted stroke sample immediately advances an input sequence and marks input pending. A preview request captures that sequence. Completion clears the input-pending state only when it covers the current sequence; an older result may still be displayed progressively but cannot claim current-input readiness. Cancellation, document refresh and failure paths clear the state. Brush compilation, seed/settings, queue replacement policy and committed document semantics are unchanged.

The `previewing` property is now worker activity OR received-input pending. It does not read a QTimer from rendering threads. A state transition is notified as soon as new input arrives, so QML can distinguish an uncommitted current preview from an update still pending.

## Acceptance
A deterministic Qt regression first completes one preview, sends another mouse sample synchronously without pumping the event loop, and immediately requires pending=true before the timer can fire. It then waits for current preview completion, compares preview and commit pixels exactly, and verifies Undo. A second case cancels while input is only pending and requires idle plus unchanged source, including after stale completion opportunities.

The test helper no longer uses arbitrary 50 ms and 20 ms sleeps as its readiness criterion: it waits for pending work and then a requested window frame swap before grabbing pixels. All earlier exact screenshot assertions remain. A frame-swap wait is test presentation synchronization, not a claim of physical display timing or universally guaranteed FPS.

No new tool or artwork is introduced. Actual painting quality, source detail limits, native Windows/stylus acceptance and the complete-work goal remain separate and unresolved.

## Focused results
Release actual Qt/QML first run passed (15.73 s), then five consecutive repetitions passed (17.68 / 15.26 / 15.29 / 16.71 / 15.49 s). The deterministic timer-pending assertions and original exact poster/preview comparisons remained enabled. Read-only review found no blocking issue in input-sequence or cancellation/failure handling. Repeated passes are not proof that all independent rendering races are impossible.

Debug actual Qt/QML also passed (1/1, 75.49 s). A freshly rebuilt 103-test local suite is being checked separately; focused success is not reported as full-suite success.
