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

Not implemented here: UI click handling, choosing merged/current-layer reference,
antialias gap closing, masks, document events, undo, serialization or export.
This is not yet a usable end-to-end paint bucket and changes no file format.
The old repaired application's GUI/58-of-60 test evidence does not validate this
new code. Artist-quality drawing is not established.

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

Keep subsequent integration on this isolated branch. Do not overwrite PR #9
or merge this partial component as if the full repair were complete.
