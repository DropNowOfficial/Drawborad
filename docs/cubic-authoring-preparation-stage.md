# Cubic authoring: bounded preparation stage, not delivered UI

## Purpose

The actual gap is editable authored shape: four persistent anchors/tangent controls can change a smooth contour after saving, rather than treating it as an opaque sampled polyline. This stage only supplies preparation/cache infrastructure and real native-render tests. It deliberately does not register engine23 in Document, storage or UI. Passing four controls directly to old19 must remain invalid. It does not auto-trace, improve source resolution, produce good composition or constitute a finished illustration.

## Geometry and memory contract

An internal candidate23 event has exactly four controls, static equal timestamps, pressure1, no tilt, fixed round width0.5..128 and opacity0..1. Direct style validation happens before copying. De Casteljau expansion uses Bernstein same-parameter chord error <=1/16 source pixel plus a conservative floating-point margin. It preserves collinear reversals/loops; the margin is not formal interval arithmetic. Per curve: depth16, visits8191, segments4095. The temporary curve buffer is bounded; global accepted expanded vertices65536, curves256 and event-list entries8192. Aggregate visited-node budget is checked at each bounded curve completion, so a rejected final attempt can perform at most8191 additional visits; this is not a hard wall-clock promise.

Prepared render surrogates use unchanged engine19 coverage, not a new claimed exact analytic cubic rasterizer. Original four-control definitions are stored separately in bounded authored copies. Legacy event geometry is not copied into the cache. Holding the source pointer keeps ownership alive but does not freeze a caller's mutable alias. Reuse therefore also verifies all candidate23 indexes and complete authored values, as well as event-list size, catching sequential in-place edits. Concurrent input mutation is unsupported; callers must synchronize ownership. Only a completely prepared result replaces the old cache; validation/allocation/cancel/budget failures preserve it.

## Verification and review

B44 uses the actual native strokeCoverage path to test exact tiled-vs-whole coverage, visibly changed numerical coverage after changing a tangent, exact original controls, source identity reuse, aggregate budgets, static-input rejection, cancelled preparation, and unchanged old19 events. Review first reproduced two defects: copied invalid largegrain before rejecting, and alias edits reusing stale cache. These were corrected with direct validation and authored-value checks. Independent regression verifies in-place handle/width/color changes, adding/removing23 and type changes, size-limit rejection before cache reuse, and legacy-only changes safely reusing cubic results. Fault injection at18 allocations and cancellation during expansion preserve old cache.

## Still required before user-facing delivery

- Shared core geometry/budget rules usable by command creation and direct CommandBus guards, not just render preparation.
- Renderer integration prepares once per frozen snapshot, not once per tile; cancellation must pass through preparation. No unbounded multiplied worker caches.
- Format23 and protected Save As, exact four-control recovery, actual old-reader refusal; no silently guessed conversion of older strokes.
- Dedicated safe create/edit commands, provenance/dependency barriers and external-worker bypass checks.
- Real Qt canvas authoring/drag handles, correct transform under zoom/pan, accurate preview/accept/cancel and Undo/reopen-edit verification.
- Complete regression and every independent build target, followed by sequential full remote CI. No Windows/physical-pen verification is claimed.

## Local integrated checkpoint

Fresh Release build and complete111/111 CTest passed in36.65s; actual Qt software canvas20.89s; final log has zero Binding loop, TypeError or ReferenceError entries. Six source/doc files differ from the complete nib-guide checkpoint. This verifies non-regression plus internal preparation only: candidate23 remains rejected by production Document/ProjectStore and there is no authoring UI. No change to old event replay is claimed or intended. Full remote CI and complete functional authoring remain outstanding.

## Complete exact-tree revalidation

The earlier local source copy omitted102 remote documentation/report/workflow files. Its111-test run was therefore not complete remote-tree qualification. This stage was reapplied to the fully verified parent source tree, preserving all703 parent paths and adding only four new paths (707 total; six changed/added files). Fresh Release build and111/111 CTest then passed in35.62s (Qt20.33s), with zero Binding loop/TypeError/ReferenceError entries. The independent alias/metadata/grain regressions and all18 allocation-failure injections plus mid-expansion cancellation were rerun against these exact-tree libraries. Parent manual source lists are unchanged; their10-target link evidence is inherited, not claimed as rerun for this internal unused module. This remains staged preparation infrastructure, not delivered persistent23 authoring.
