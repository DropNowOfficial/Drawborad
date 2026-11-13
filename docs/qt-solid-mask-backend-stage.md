# Qt solid-mask backend: bounded preparation stage (WIP)

This is an optional development adapter, not a delivered artwork, editable solid-shape feature, or new file format. Enable `DRAWBORAD_QT_SOLID_MASK_CACHE` to build its independent library and focused test. It is OFF by default and is not linked into application replay, export, core, or portable render targets. Existing engine 1–24 semantics remain unchanged. Existing readers must continue rejecting unsupported newer engines; no new event is admitted here.

## Why whole cohorts

The current renderer visits events inside each requested tile. A one-entry per-event full-canvas cache would repeatedly rasterize overlapping events. This adapter prepares a bounded cohort before tile iteration and only copies pixels during tile reads. It deliberately refuses oversized cohorts rather than invisibly thrashing an LRU cache. Successful preparation publishes all masks together; a failed/cancelled attempt retains the previous cohort. Exact unchanged cohorts can reuse their masks.

Native Alpha8 masks are the single raster source. Crops are not independently painted, scaled or resampled. Geometry, canvas domain, raster policy and Qt runtime version form cache identity. No cached QImage is exposed for callers to mutate or retain outside accounting. The owning thread must serialize use; this is not a concurrently shared mutable cache. The eventual UI integration must run preparation in its existing worker/snapshot flow, not synchronously on the UI thread.

Budgets cover image payloads, including row stride and simultaneous old/candidate storage. They do NOT bound Qt internal scratch, input vectors, caller-owned tile storage, all application memory or total RSS. Cancellation is observed before allocations and after painting; a single QPainter call cannot be interrupted. Production admission must also reserve the application's merge/export buffers and actual measured overhead.

## Backend and compatibility boundary

This adapter requires QtGui. Portable tools retain their Qt-free dependencies and do not gain solid-shape support merely because the optional target builds. Before persistent solid events can exist, all load/render/export paths need explicit backend-unavailable/version-mismatch failures and a coordinated new format with old-reader rejection. Never return blank pixels for an unsupported persisted event. Qt 6.8.2 Linux evidence does not certify Windows Qt 6.12, matching pixels across Qt versions, physical printer color, or pen hardware.

## Licensing and distribution

No dependency is installed or vendored by this stage. It dynamically links the existing QtGui/Core SDK. Qt headers and package notices offer commercial or specified LGPL/GPL terms; using an existing SDK does not remove distributor obligations. Existing Windows SDK-notice copying is not proof of complete LGPL compliance. Before distributing a binary, verify the exact shipped Qt build and applicable license, preserve required notices/license texts, meet source/relinking requirements where applicable, and do not claim license qualification based only on a passing test. This stage introduces no new Qt binary redistribution. The installed `qpainter.h` identifier is `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. For an LGPL3 distribution, the checklist includes prominent Qt notices, LGPL3 and GPL3 texts, applicable third-party notices, permitting compatible modified Qt replacement and debugging reverse engineering, and the applicable corresponding source/installation information obligations. Shipping dynamically linked DLLs alone does not establish compliance; static distribution has additional relinking requirements. The existing SDK notice manifest is useful evidence, not a legal-compliance certificate.

## Remaining work

- Integrate the prepared provider with explicit capability/admission failures, without changing old replay.
- Resolve Qt-version raster policy before introducing persistent new shape semantics.
- New shape authoring, editing, history, Save As protection and old-reader refusal.
- Real desktop preview/cancel/accept, PNG/ORA equivalence and source preservation.
- Actual Windows validation and independent visual artwork review.

Remote PR10 tests remain unqualified while GitHub billing/limit prevents jobs from starting. Local evidence must be reported separately, and this code must not be merged as a qualified release.

## Measured scope and hard limits

The adapter exposes six authored controls (two closed cubic spans), with Qt OddEven fill semantics, including self-intersections. It does not certify a simple lens topology or provide automatic contour repair. Traversal is authored order; reversal-equivalent pixels are not promised. Color, opacity and layer compositing are outside this alpha-only cache and require separate composition identity. A publication generation is required on every tile read, so an older published cohort cannot masquerade as a newly accepted request.

On cloud Linux / Qt 6.8.2, the frozen standalone native test measured three 1725×2625 masks at 13,608,000 image bytes and approximately 18.4 ms initial preparation; atomic changed-cohort replacement fit the limit. Two 3300×5100 masks used 33,660,000 bytes and approximately 27.1 ms initial preparation. Their atomic replacement requires 67,320,000 image bytes, exceeding the fixed 64 MiB cap, and was correctly refused before rasterization while preserving the old generation. This is a real usability limit, not a completed high-resolution editing workflow. Timings are individual observations, not latency guarantees or whole-application memory measurements.

## Follow-up: immutable-record reuse (supersedes all-replacement limit above)

The follow-up implementation separates event IDs/order from internal immutable mask records. Exact controls, native domain and Qt runtime identify a raster record. Unchanged old records and identical candidate geometry are reused without writing or detaching their QImages. Only fresh records are mutable while being painted; no mutable pointer escapes publication. Cohort changes still advance generation even when no raster call is needed.

Admission now counts old unique image records plus genuinely new unique records; retained bytes after publication count the candidate's unique records. It does not infer ownership from QImage reference counts. Two 3300×5100 masks with only one changed geometry use 50,490,000 bytes at publication overlap and succeed. Changing both still exceeds the 64 MiB budget and is rejected before painting. Removing/reordering/renaming IDs does not force unchanged masks to repaint. Color and compositing remain separate responsibilities.

This resolves one important atomic-edit memory limit but does not add a persisted solid event, renderer/export integration, desktop interaction, whole-app memory guarantee or Windows qualification. Original whole-cohort checkpoint remains preserved separately.
