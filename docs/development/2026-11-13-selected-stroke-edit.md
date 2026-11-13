# Safe selection of earlier solid-ink strokes

## Actual workflow obstruction
The prior bounded editor could only repair the global last event. Drawing a second independent detail made the previous contour uneditable, even though its raw samples still existed. Broadening that rule blindly would be unsafe: a later bucket fill or hollow correction may have sampled the old boundary, so moving it while silently retaining the derived color geometry can misalign the result.

## Conservative extension
Select an explicit solid-ink event on the active editable layer. The desktop lists at most the newest128 eligible events, newest first, and displays the existing sample handles for the selected ID under the current canvas transform. It does not infer artistic intent or pick a distant/invisible handle. Selection is transient, not a new project format. Switching layers/opening/Undo clears selection.

At both command and helper boundaries, all subsequent events must be validated independent native hand-mark formats1/2/10/12/19 on native layers. Fill/hollow format8 and unknown formats are a dependency barrier, even if on another layer: no dependency graph is inferred and no color region is silently regenerated. All event ordering, other-event geometry, resources and target style/time metadata remain exact. Later erasers retain their own geometry and are composited normally; this does not mean the final pixels cannot change when an earlier stroke moves.

Candidate render, explicit accept/cancel, bounded worker, stale document checks, Undo and native save/reopen remain required. No automatic redraw, generated detail, node insertion, Bezier fitting or private art upload. This is a manual correction tool, not evidence that the artist-quality goal has been achieved.

## Acceptance
Release full suite passed106/106 in21.09s (-j4). Actual Qt/QML dropdown selection, transformed point hit, earlier-event preview/accept, exact later-event preservation, Undo/save-reopen and subsequent sampled-fill refusal are covered. Independent review also found the pre-existing ability to switch layers during busy/pending review: both the API and layer-list UI are now blocked and tested, so the visible current layer cannot diverge from the frozen edit target during acceptance. Core tests include later format1/2 eraser/10/12/19 eligibility and format8/unknown command-bypass refusal. These are Linux software-rendering and synthetic fixtures, not Windows/physical-pen/art-quality validation.
