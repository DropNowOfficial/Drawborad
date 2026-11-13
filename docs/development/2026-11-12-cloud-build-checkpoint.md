# Cloud build and preservation checkpoint — 2026-11-12

## Goal
Continue toward complete high-quality, naturally drawn, editable layered cover/comic work. Native print-size drawing, independent color fills below preserved line art, varied controllable marks, saved/reopenable documents, and non-destructive edits are practical milestones. Passing engineering tests is not artistic acceptance or a claim of human-artist equivalence.

## This checkpoint
Preserves previously unpublished optional MyPaint tint, dry preset, radius multiplier and Normal raster compositing; empty ink/dab bounds checks; crop masks; source-aware hidden fill boundaries; picker and palette controls. MyPaint strokes remain immutable cropped raster layers, not point-editable vectors. The complete Windows GUI has not been validated.

A cloud-only local toolchain was installed from the signed Debian trixie repository: CMake 3.31.6, Ninja 1.12.1, Qt 6.8.2, PNG/zlib/JPEG/TIFF/WebP/LCMS development libraries. No user computer changes. This satisfies the Linux headless requirement, NOT the Qt6.12/MSVC/D3D11 Windows requirement. Source was reconstructed from published 6c75065 plus retained local source overlays; no lost work is invented.

The first full compilation exposed a missing closing brace in stroke-mask JSON serialization. Fixed at its source. The Normal paint integration fixture previously imported a 3300x5100 raster beyond the existing 128MiB conservative decode estimate; it now creates that canvas and black backdrop natively and asserts every baseline pixel is opaque black. Import limits remain unchanged. Isolated pen/bucket handler tests now supply the new inactive boundary/picker controls in their mock context.

## Verification
- Complete Linux Debug headless build succeeded.
- CTest: 85/85 passed, 0 failures (includes native 3300x5100 poster/export, independent ORA checks, Normal composition, mask/hidden-boundary sessions, undo/redo and save/reopen).
- Six JavaScript handler test files passed. These use VM/DOM stubs, NOT real browser UI acceptance.
- Native smoke generated PNG, layered ORA and .draw; replay after reopening matched. This is a synthetic technical fixture, not final artwork.
- Full cloud Python suite results are recorded in the companion verification JSON.

## Still not complete
- No current Chromium GUI acceptance from this cloud session; prior socket restrictions remain distinct from headless tests.
- No Windows desktop release, installation, Qt6.12 qualification, or Photoshop interoperability claim.
- Full-frame 3300x5100 raster import can exceed its conservative 128MiB decode gate; native canvas creation and cropped painting are the tested path.
- Format18 semantic contour experiments are separate from this main integration checkpoint. Private reference-derived fixtures remain private backups, not this repository.
- Reference interpolation and raster tone correction do not recover missing high-resolution detail. Complete artist-quality cover/comic output is still unmet.

No private reference images, derived art, credentials, SDKs, build caches, or local ports are committed. PR #10 stays draft; no merge or PR #9 change.
