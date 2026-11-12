# Bounded bucket selection reconstruction

This is a newly reconstructed small component based on PR #7
f9085f9934b5f0c2dba42b0433ecde15c862a794, not recovery of the inaccessible
v3-v8 repair source.

Implemented: a native C++ read-only flood-fill selector returning disjoint,
row-sorted runs. It uses 4-connectivity, seed-relative per-channel tolerance,
transparent-RGB equivalence, bounded canvas allocation and run count, and
cooperative cancellation. No partial mask is returned on failure.
The default supports the 3300 x 5100 print canvas and caps input at 32 Mi pixels
and output at 65,536 runs. The caller must provide a rendered RGBA8 reference;
the input itself is never changed.

Follow-up integration adds a native v8 bucket event, exact pixel-span coverage,
undo/redo through Document, merged-reference selection, a separate color layer,
browser click handling and a save-copy flow. New snapshots containing v8 use
DRAW-DELTA-8. Unknown versions fail closed; mixing legacy and v8 primary/backup/
journal or overwriting across formats is rejected. Already released old readers
are not repaired. Keep newer projects in separate copies.

Still unverified: actual Qt session compilation, full native session test and
real GUI. Not implemented: antialias gap closing or masked-target fill (rejected),
or current-layer reference selection (merged visible image only). The existing
full-document renderer still has its original 4 Mi pixel/layout limits; the
3300 x 5100 selector test does not make the GUI support that canvas size.
The former application's GUI/58-of-60 report does not validate this reconstructed
code. Artist-quality drawing is not established.

Fresh validation:
- Test-first basic enclosed-region assertion failed on the explicit unimplemented
  function, then passed after implementation.
- 3300 x 5100 default-budget regression failed before the default was corrected.
- Native tests cover border seeds, disconnected diagonal pixels, malformed
  dimensions, pixel/run budgets, cancellation, tolerance, transparent pixels,
  input immutability, 500 seeded random images against an independent BFS oracle,
  and a 16,830,000-pixel print canvas.
- Compile/run directly with:

  g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I src tests/brush/test_flood_fill.cpp src/brush/FloodFill.cpp -o /tmp/test_fill
  /tmp/test_fill

- CMake registers B08.BoundedFill, but the full CMake build/CTest suite cannot
  run in this reconstruction executor: CMake and Qt development tools are absent.
  No new desktop tools were installed.
- Address/undefined sanitizer run uses ASAN_OPTIONS=detect_leaks=0 because this
  executor's ptrace environment prevents LeakSanitizer from running. Leak
  checking is not claimed.

Additional fresh validation: native bucket event apply/render/undo/redo and
malformed-run/lock/visibility/transform guards pass with ASan/UBSan (same LSan
limitation). Storage tests cover format roundtrip, unknown-primary no fallback,
bidirectional old/new overwrite rejection and mixed/orphan sidecars. Original
F04 recovery suite passes 14 cases. Six Python API transport tests and JS UI
helper/click tests pass, but their transport stubs are not native acceptance.
Python tests emit an existing Starlette/httpx deprecation warning.
A real headless session test is registered for CI; it has not run locally.

Keep subsequent integration on this isolated branch. Do not overwrite PR #9
or merge this partial component as if the full repair were complete.

## Resume checkpoint (2026-11-12)

Goal: a genuinely usable, editable, layered native drawing workflow. This bucket
reconstruction is one component, not a claim of artist-level drawing or recovery
of the lost prior repair.

Persisted checkpoints on PR #10:
- f6e4d526: bounded selector + tests, six remote files read back exactly.
- 88b3ddf2: v8 event/renderer, session bridge, UI/API and format-safe save-copy;
  branch head confirmed and Session/Store/UI read back exactly.

Verified locally: selector, real native event rendering/undo/redo, storage safety,
14 original F04 recovery cases, six stubbed-transport API tests, JS click guards.
Qt bridge and full suite are NOT locally verified. No current real GUI acceptance.
GitHub native run 43180146828 and cloud run 43180146754 started for 88b3ddf2;
results were pending when recorded. Never treat started as passed.

Next work, in order:
- [ ] Read CI for the exact latest code SHA and fix real build/regression failures.
- [ ] Run the real BucketSession test and inspect actual saved/loaded pixels.
- [ ] Test actual browser fill, undo, redo, separate-copy save, reload and download.
- [ ] Confirm file delivered and layers independently editable; document limits.
- [ ] Revisit fine brush quality, semantic layers and original-art quality separately.
- [ ] Preserve each runnable increment remotely. Do not overwrite PR #9 or merge.

Current limits: no masked-target fill or gap closing; merged visible reference only;
original full-document 4 Mi pixel renderer budget is unchanged. Existing released
old readers cannot be patched by these files, so new-version copies remain separate.

Independent review found that adding a layer could exceed the current preview
memory budget after mutation. A real native regression reproduced this at 2048²
with three existing nodes; the new preflight now rejects a fourth node before
mutation. The test is GREEN. This deliberately does not raise rendering limits.
