# Nominal mouse nib guide

## Problem and scope

The desktop previously showed no nib/eraser size before a mark. Precise contour/negative-space work at native print resolution therefore required a trial stroke just to see the selected width. This change supplies a non-persistent pointer guide, not automatic drawing, image enhancement or a claim of artistic quality.

The chosen solution is an independent QML overlay driven by CanvasItem's current item-space hover point and the existing document transform. Alternatives were changing the renderer/export image (rejected: would contaminate art), or a fixed screen circle (rejected: wrong source-pixel size under zoom). No new project format or source pixels are introduced.

## Contract

- Mouse and touchpad hover only. Non-mouse devices, including stylus and airbrush, have no nominal guide; their real stroke preview remains available. This avoids claiming the selected chisel matches a hardware eraser tail.
- Full-pressure nominal width. Round tools/controlled eraser are circles; fixed chisel uses3:1 axes and angle k×22.5°. It does not model actual pressure, texture, taper, AA edge coverage or validate permission to erase a dependency-bound layer.
-64 bounded points, same uniform document-to-item scale as the real canvas, current item-space center retained through zoom/pan. The black/white screen-space outline and small center cross do not represent paint thickness.
- Hidden outside item/document, while painting/panning/busy/replaying/rendering/error, in fill/selection/gap modes, when disabled/invisible or no paintable active layer, and for CMYK. Hover leave, focus loss, window deactivation, document change and size change clear stale position. Mouse press/move/release clears the guide; subsequent hover restores it rather than resurrecting an old pointer.
- QML overlay has no MouseArea, is clipped to the canvas and never enters an event, snapshot, save or export. Original artwork and Undo history are not modified by hovering or zooming.

## Verification plan

Actual Linux Qt software/QML: non-square1200×1800 canvas, fit/0.25/16 zoom and pan; center/axis geometry tolerance.01 item pixels, diagonal and90° chisel directions, circle eraser, high-zoom outside-item negative case, non-mouse device rejection, leave/tool/disabled/deactivate suppression, screen guide visibility and clearing, real window click/drag passes through to drawing, and snapshot/render equality before/after guide-only interaction. Windows, physical devices and additional display-DPI combinations remain separate acceptance work.

## Existing selection notification defect found during validation

Repeated writes of the same rectangle (including corresponding NaN coordinates from blank fields) emitted the broad brush-settings notification, which fed the QML bounds Binding's `when` dependency back into itself. Bounds now have their own notification; unchanged components return without emission, real changes still clear the polygon, and direct drag completion also emits the dedicated notification. Invalid NaN values remain invalid, never silently replaced with zero. Existing rectangle/lasso/preview regressions remain part of the complete suite.

## Verified checkpoint

Final local Release run:109/109 passed in21.64s, actual Qt software/QML21.63s. The final Qt log contains zero binding-loop warnings and no TypeError/ReferenceError. Tests include the actual window mouse hover/click path, visible guide screenshot, input passthrough, hiding during stroke, cleared-guide screenshot, transformed geometry and unchanged document/render state. A preceding failing screenshot assertion was traced to mixing a synthetic hover coordinate with the real mouse hover position; the test now moves the real window mouse before sampling the screen. The guide's visual output was personally inspected. No diagnostic screenshot is included in source uploads.

## Rebased local validation

Revalidated on the repaired continuous-eraser base, retaining the native source-list guard and all three repaired manual target link lists. Fresh `brush-footprint-rebased-build` configured Release, HEADLESS_WORKFLOW and QT_CANVAS_TESTS enabled, with the explicit primary Python3 interpreter. Complete build succeeded; all110/110 CTest cases passed in85.55s, including actual Qt software canvas21.07s and Build.NativeCoverageLists. Final LastTest.log has zero Binding loop, TypeError or ReferenceError entries. The independent source-list check also passed3/3.

Compared with controlled-eraser-rebased-work, the source differences are exactly the five files in BrushFootprint_paths.txt. No previous frozen tree was changed. This is a local checkpoint, not a GitHub publication, Windows qualification or completed illustration. Parent commit will be assigned by the publishing coordinator.

## Complete repair-chain validation

Rebased again on controlled-eraser-complete-work, whose parent is the complete five-target native CI repair rather than the earlier incomplete three-target check. New guide parent commit9b89d2af66df5a314bf87334e129f63ea43211e4 (tree9eaa9c0f3b9e1d9ec711d3def0388f16b1ebbc31). The guide remains exactly five changed source/document files.

Fresh Release HEADLESS_WORKFLOW+QT_CANVAS_TESTS build with explicit primary Python3 completed; full CTest110/110 passed83.87s and actual Qt software canvas20.79s. Final LastTest.log has zero Binding loop, TypeError or ReferenceError entries. This specifically removes the existing bounds-Binding warnings retained in the eraser predecessor.

All manual build scripts, core/render dependencies and README compiler lists are byte-identical to the complete eraser predecessor, so its actual eight linked-output checks and four experiment suites apply without rebuilding them here; they were not newly rerun for this guide. The new full build and tests above were rerun. No GitHub upload, remote-CI pass, Windows/physical-device qualification or finished illustration is claimed by this local checkpoint.

## Exact remote-tree checkpoint (supersedes partial-copy coverage)

Earlier local copies omitted102 remote files, including an additional contour-flow compiler recipe. Their passing local tests did not establish equivalence to the full remote tree. This checkpoint was reconstructed from all694 verified remote manifest blobs plus the authorized eraser additions, retaining702 parent source files before overlay and703 after adding this guide document. No build/proof artifacts were copied. Frozen Git metadata was copied only for the existing CMake provenance path.

Exact eraser parent7d9e3db66b7f1dda3f6b1f5469a3f470cca14adf, treeace99c576fbe4d4bedce7b1c7ab87157ef82ef61. Parent feature/contour validation documents were synchronized without changing guide code. Final source differences from that parent are exactly the five guide allowlist files.

New full fresh Release HEADLESS_WORKFLOW+QT_CANVAS_TESTS build with explicit primary Python3 succeeded. All110/110 CTests passed88.20s, including Qt software/QML20.46s. Final log has zero Binding loop, TypeError or ReferenceError entries.

Manual compiler scripts, renderer/core sources and all document source lists remain byte-identical to the exact eraser parent. Its verified ten renderer-dependent output links, four experimental suites (65 tests total) and E02's18 tests are reused, not falsely described as rerun for the guide. This corrects the older eight-output scope by including E02 and contour_pigment_evidence. Local checkpoint only; remote CI, Windows/device qualification and final-art quality remain separate.


## Complete inventory-order parent acceptance

Reassembled above the complete 703-file inventory-repaired eraser parent, retaining all parent content and adding only the nominal-guide document (704 source paths total). Feature delta remains exactly the five allowlisted guide paths. TestTargets finalization is last, after B43 and the actual inventory comparison regression.

Fresh Release HEADLESS/QT_CANVAS build was initially interrupted at 70% to avoid concurrent memory pressure, then resumed with **-j1** without source changes. Full CTest passed **111/111 in 88.03 s**, including actual Qt software canvas (21.61 s). Final LastTest.log has **0 Binding loop, 0 TypeError, 0 ReferenceError**. Actual inventory equals CTest with 111 unique entries including the comparison test itself.

Manual renderer/core/script/document compile sources remain byte-identical to the exact eraser parent. Its ten actual link outputs, 65 experimental-suite tests and E02 18 tests are reused evidence, not rerun here. This is local software validation, not Windows hardware or artwork quality acceptance.
