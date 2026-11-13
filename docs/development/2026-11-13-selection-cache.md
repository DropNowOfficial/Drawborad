# Read-only stroke selection cache

The new manual selection UI repeatedly asked for eligibility and point data while the view moved. A synthetic valid244-stroke/999424-sample native document measured full eligibility at roughly6.5ms per latest-event request (10.5ms for oldest) plus approximately2ms for the128-item menu. These are core-call measurements, not frame timing.

Menus now cache against a held immutable snapshot identity: document ID, revision, node/resource/event pointers, document spec and active layer. Point lists also key by selected event ID. Source changes, Undo/open, layer changes and edited geometry invalidate the result. Busy/pending/replay still return no selection data. This is read-only display caching; preview workers and the command boundary continue to validate the complete current snapshot, dependency barrier and candidate render. No write authorization or validation is cached. The cache holds immutable snapshot references and therefore is not zero-cost storage; resetLayerSelection releases them on open/Undo, while other content changes replace them on the next eligible read.

Acceptance: full local Release106/106 passed29.20s (-j4). QML regression exercises changed geometry after accepting a correction, old/new selection, cross-layer and pending-state behavior, subsequent-fill barrier, Undo and reopened projects. An independent actualQt getter harness uses the exact same valid synthetic project before/after with1000 paired menu+point calls; baseline14,541ms, cached0.6439ms with nanosecond-timer resolution. These measurements exclude rendering, viewport point mapping and physical pen input; no frame-rate/large-paint speed claim.

No private image is involved. The manual artistic decisions and high-quality faithful artwork remain unfinished.
