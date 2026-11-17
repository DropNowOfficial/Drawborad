# Native photo quality checkpoint — 2026-11-17

Base PR #10: dc8a930078ed690d95936665695145af2172105e.
Implementation: PR #11, work/quality-multiscale-20261116.

Actual original-size inputs: CC0 Chelsea 451x300 and coffee 600x400.
Both masks are explicitly whole-image fixture-no-ai diagnostics. No SAM3,
semantic object recognition, paid GPU or deployment was executed.

The old production sweep failed its 8192-event planning budget on both photos.
For a real visual baseline, the unchanged sweep was run with an explicit 65536
DIAGNOSTIC event budget: 20890/34810 events. The new production opt-in produces
30613/32769 events including guides, without downsampling. Thus both algorithm
and available production budget changed; this is not an equal-budget benchmark.

Visual inspection: eyes, whiskers and spoon highlights are more continuous;
the repeated horizontal stepping is reduced. Fur still has an etched look,
and smooth cup shading remains simplified. Pixel error is diagnostic, not
an artistic-quality pass. Full/ROI metrics and export/edit results are in the
adjacent JSON file. No claim of a complete quality breakthrough is made.

Passed locally with GCC 14.2, Qt 6.8.2, Release:
- 123/123 native CTest after ROI-render and merged-budget changes.
- 105 Python API/batch tests and 23 subtests; 12 deprecation warnings.
- Both full photo projects: actual native recolor, changed pixels, undo/redo,
  stale/locked rejection, save copy/reopen, zero/mid replay, PNG/ORA.
  Source images were temporarily moved aside and restored after the checks.
- Independent ORA composition matches both original and edited PNG exactly.
- Independent HTTP: fixture upload/review, selected-object multiscale build,
  16955 events, source-free replay, restart then review/download.

Failures and limits retained:
- Initial build tests lacked TIFF/WebP development dependencies: corrected,
  then complete native suite passed. Python test launch from wrong cwd was
  corrected; no failing assertion was disabled.
- Browser launch failed before navigation: Chromium ProcessSingleton socket()
  returned Operation not permitted. Browser acceptance is NOT passed.
- First PR11 Actions runs 44019725980 and 44019725994 failed before any job
  steps/runner assignment. Do not call local tests remote CI success.
- Input remains limited to 4 MiPixels, opaque RGB8 and binary reviewed masks.
  3300x5100, soft hair edges, automatic semantic ownership and Windows unverified.
- Cubic/geometry-edit commands retain their 8192-event guard. Recolor works on
  these larger projects; other editing modes must not be advertised as verified.

Provenance: local build report gitSha aeaa01a1645e67b1fbe46dfd323c917eb642c14c
is the bootstrap commit of a hash-verified mirror of PR10's 755 blobs, not a
remote feature commit. PR11 published the planner. The final renderer/editor fixes are local. Apply from-pr10.patch to the exact PR10 base to reproduce the full tested tree. Historical report fields are retained rather than relabeled.
