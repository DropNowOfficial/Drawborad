# Bounded asynchronous display composite

User goal: continue reducing desktop stalls without losing editable data. Builder
choice under delegated implementation: first move only display compositing off the
GUI thread. File open/save and atomic recovery remain unchanged in this stage.

Alternatives: async file saves affect dirty-state/close transactions and need a
separate design; unrestricted render futures can accumulate full poster frames.
Use a private one-thread Qt pool instead: at most one running and one newest pending
job per canvas. A newer snapshot cancels the previous stop source and clears queued
work. Generation-tagged results are accepted only for the current canvas request.

Snapshot/resource ownership is immutable shared state. Worker code never accesses
the bridge/QML/scene graph. Completion is posted to the GUI thread. The canvas clears
outdated pixels, displays a rendering indicator, and blocks stroke input while busy.
Destructor cancels, clears queued jobs and joins before QObject destruction. No
worker can write project files. Legacy native-only transform fallback remains.

Validation: actual QML busy indicator, queued stale results rejected, event-loop
heartbeat while poster composites, drawing blocked while busy, final supported/error
results, and existing poster paint/save/reopen source equality. Windows and file-I/O
responsiveness remain explicitly unverified/out of scope.

Implementation safety details: one completion mailbox holds the newest result;
queued callbacks carry only a generation, not full poster images. Changed snapshot
or replay cancels an in-progress mouse/tablet gesture. Tests count event-loop ticks
only while rendering is true, verify final stroke pixels after rapid replay changes,
and exercise destruction after a short processing window. Cancellation is cooperative:
resource validation/planning may finish before checking stop, so teardown is not
claimed instantaneous. File save/open remain synchronous and are not changed here.
