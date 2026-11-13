# Cubic authoring: persistent core stage (UI still pending)

This succeeds the preparation-only checkpoint documented in
`cubic-authoring-preparation-stage.md`. That document records historical tests;
its statement that production rejects engine 23 describes that earlier checkpoint,
not this branch's current core implementation.

## Implemented contract

Engine 23 (`cubic-ink-aa`) stores exactly four authored controls, not a flattened
polyline. Its fixed round nib has width 0.5–128 source pixels, fixed pressure and
opacity, no grain or tilt. Geometry expands through the bounded shared helper to
unchanged engine 19 coverage. This is not analytic rasterization, automatic
tracing, variable-width calligraphy, or restoration of missing source detail.

Creation and a dedicated single-action control edit validate the full snapshot,
coordinate bounds, writable native layer, event identities, and bounded source
and expansion budgets. Append admission checks malformed incoming geometry and
legacy-history size before copying the history. Handles may extend outside the
page only within [-width, 2*width] and [-height, 2*height].

Editing rejects recorded fill dependencies and later events whose independence
cannot be proven. Later independent handwork is preserved exactly. Generic
ConvertColor is explicitly unsupported on documents containing engine 23 because
that legacy operation accepts replacement geometry. External workers cannot
append this authoring version. Undo/redo restores original authored controls.

The renderer prepares cubic geometry once per snapshot, passes cancellation into
preparation, and revalidates existing cubic cache identities against sequential
mutable aliases. Concurrent mutation is not supported. Legacy-only renderer
snapshots retain their immutable-input contract; this is not a blanket mutable
snapshot API.

## Persistence and compatibility

DRAW-DELTA-23 retains four controls and explicit fill-operation records. Existing
formats 1–22 are not rewritten speculatively. ProjectStore uses protected Save As,
version-aware generation/journal/backup handling, and rejects incompatible reuse.
A real older linked reader was exercised against the new fixture and rejected it
with `project.engine-version`, leaving the input file unchanged. This is a
one-way format boundary, not backwards-readable conversion.

## Verification so far

B44 preparation, B45 persistence and B46 core authoring passed together after the
append-budget and renderer cache review fixes. B46 checks actual changed pixels,
preparation reuse across tiles, undo/redo, later-handwork preservation, locked and
dependent edits, bounds, multi-action and worker bypass refusal. Full-suite and
independent manual-target evidence must be recorded separately after completion.

## Still outstanding

No user-facing four-control drawing or dragging exists in this stage. Required:
actual Qt handle hit testing, zoom/pan mapping, exact preview, accept/cancel,
selection safety, save/reopen/edit and negative interaction tests. No Windows or
physical-pen test is claimed. The complete art-quality gap remains: construction,
composition, material-specific marks, and faithful artwork are not established
by these engineering tests.

## Desktop implementation plan

`cubic-qt-authoring-plan.md` records the reviewed next-stage design, including
explicit draft/save/close guards. It is a plan, not implemented UI.

## Completed local core checkpoint

The initial parallel complete build exhausted memory at the large Qt test
translation unit (compiler killed); this was not counted as success. A serial
retry completed the full build and all 115/115 tests in 89.01 seconds, including
the actual Qt software canvas test (21.15 seconds), B47 allocation/cache
regression and actual 115-entry CTest/inventory equality. Qt diagnostics contained
no Binding loop, TypeError or ReferenceError. The four-control GUI itself is
still unimplemented; the Qt pass is existing UI non-regression, not new drag
interaction acceptance. Ten standalone native link targets are being rebuilt
separately; do not infer their success from CMake tests.

The standalone audit then rebuilt all ten renderer-dependent native outputs
successfully: five experimental outputs, three documented artwork builders, and
the two contour targets. E02 ran 18/18 successfully. Image-producing programs
were linked but not run. The four Python experimental suites were not rerun in
this audit; no inherited suite result is presented as a new execution.
