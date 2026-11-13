# Controlled continuous eraser, engine22

This new mode erases alpha from the chosen native hand-painted layer. It does not paint white, rasterize/replace the original, erase other layers, regenerate artwork, or remove old events. Existing physical/legacy engine2 eraser replay remains unchanged.

## Contract

- engine22, toolId `eraser`; width.5..128px, pressure0..1, opacity0..1, Round/angle0/hardness1, seed0, no grain. Internal color is canonical zero-channel RGB with alpha1; tool opacity controls erasing strength. Valid tablet tilt/device metadata is preserved without changing this circular nib.
- Coverage delegates via a temporary validated engine19-shaped event to the **unchanged** continuous circle routine. The stored event remains22/eraser and existing PixelBlend performs destination-out alpha on that layer. Old19 rendering and saved events are not rewritten.
- Raw points<=4096; helper existing+new points<=1M, events<8192 before append, nodes<=4096. Existing per-tile coverage work and cancellation limits apply. Partial-time playback, repeated point handling and antialias semantics match engine19; no new texture.

## Conservative permission/dependency boundary

`continuousEraserLayerEligible` requires a native paintable layer without sourceResource/content. Any recorded fill operation referencing it as source, target or boundary refuses the action. Starting at that layer's earliest event, every remaining event in the document must be an independent native hand mark; unknown/derived/bucket events block erasing even on a different layer. This deliberately does not guess causal independence from bounds or color.

The same eligibility is checked again in CommandBus for engine22 Paint. The command must contain exactly that one Paint; batch tricks do not bypass the check. No old event is removed or rewritten. UI may additionally reject the currently selected fill-boundary layer before any fill has occurred. Existing imported source art remains unavailable to this tool.

A new, unrelated layer first drawn after existing fills can be erased because those earlier operations cannot have sampled its future strokes. Merely having no record does not make later derived operations safe; they remain a barrier. Engine22 can be retained as independent later handwork for other existing tools, but remains ineligible for engine19-only local edit/close/closed-fill ownership.

## Persistence

Format22 takes precedence and always writes a fill-operation count (zero or more), preserving metadata. Formats1..21 continue their old encodings. Old readers reject22. Cross-generation overwrites/journals are refused, requiring a new filename. Undo returns the earlier complete state; no silent downgrade overwrite is permitted.

## Evidence and limits

B43 checks current-layer half/full transparency, exact unchanged lower-layer pixels and original events/resources, equality to legacy19 coverage, malformed style/size/budget/cancel refusal, imported/recorded/unknown-derived dependency rejection including direct CommandBus bypass, Undo/native reopen and protected save. B42 historical golden checks remain separate regression evidence. Full Qt interaction/Windows/physical pen testing is separate. This is editing infrastructure, not completion of the user's artistic quality goal.

## External worker guard

Review found that worker deltas entered through `Document::commitExternal`, not CommandBus. That entry now rejects every newly appended engine22 event (`worker.continuous-eraser-not-supported`); a previously committed engine22 prefix may remain unchanged. This deliberately does not enable remote-worker erasing. The direct CommandBus path also limits old plus new geometry to 1M points, not merely the old snapshot. B43 exercises the actual JobBroker acceptance path with an otherwise valid delta that preserves all fill records yet tries to append an eraser, requires refusal and unchanged state, and covers preserved old22 prefixes and the raw-Paint aggregate-point boundary.

The new safety rules apply to the explicitly enabled controlled eraser. Existing historical engine2 eraser replay and the old hardware-tail input mode are not rewritten or retrospectively claimed to have these new guards.

## Desktop verification checkpoint

After both review fixes, the full local Release suite passed109/109 in21.13s; actual Linux Qt software/QML integration passed20.59s. It drives the real controlled-eraser switch and mouse packets, checks the displayed half-erased pixel exposes the green lower layer rather than white paint, confirms cancellation before commit, exact old event preservation, full erase, mode exclusion, selected-boundary refusal, Undo/redo, generation-mismatch save refusal and pixel-identical reopen. This is not a physical pen or Windows hardware test.

Both the preserved older reader and a reader linked against the immediately preceding engine21 library refused the new22 fixture with `project.engine-version`; the native-file SHA256 stayed unchanged. A diagnostic test is not a finished illustration; the user's art-quality goal remains unmet.

## Reparenting after isolated-build CI repair

The unpublished original checkpoint is preserved, but its parent chain was superseded by845c455. This rebased checkpoint inherits per-target coverage checks and explicitly adds ContinuousEraserCoverage.cpp to all3 isolated native link targets. All3 targets rebuilt; automatic-stage15/15 passed2.896s and flow15/15 passed1.787s. Fresh full Release110/110 passed36.59s including actual Qt23.29s, B43 worker/dependency regressions and the source-list guard. This is still pending its own complete remote CI after predecessor success.

## Complete parent-chain validation (scope correction)

The preceding historical “all3 isolated native link targets” statement covered only automatic-stage plus the two flow outputs. That scope was incomplete: it omitted hybrid and locality, so those earlier successes did not establish all-CI-target coverage. This checkpoint is instead based on complete native CI repair f96fc904b2bf19ae0710ca4a99019e63b28428a8 and retains its dynamic10-test source-list guard unchanged.

All five independent CI outputs were newly built with ContinuousEraserCoverage: automatic native-plan, flow libflowfeedback.so, flow native-flow, hybrid native-hybrid and locality native-locality (hybrid also rebuilds flow). All four actual suites passed: automatic15/15 in3.692s, flow15/15 in1.886s, hybrid15/15 in2.203s, locality20/20 in0.320s. Additionally all three README example outputs, BuildCoverRegions, BuildFaceStudy and BuildDryInkStudy, compiled and linked using the documented source lists with the available toolchain libpng paths. Those three executables were not run to generate any image. Thus coverage here is five CI outputs plus three documented outputs, not three targets.

A fresh Release build with HEADLESS_WORKFLOW and QT_CANVAS_TESTS enabled and explicit primary Python3 succeeded. Full CTest110/110 passed in82.14s, including actual Qt software canvas19.40s and the dynamic native coverage guard. The original bounds-Binding warnings still occur in this eraser UI version; their separate mouse-guide successor fixes them and is not silently included here.

Source differences from the complete repair base are exactly28 allowlisted files (23 eraser feature files, four build scripts and the artwork README); generated build/cache files are excluded. The parent validation document was synchronized byte-for-byte. This is local validation, not remote-CI success, Windows or physical-device qualification, or finished-art delivery.

## Exact complete-tree revalidation (additional scope correction)

The earlier “complete repair base” copy was still a partial checkout:102 documents were missing, including the contour-flow portable recipe. The28-file and eight-output results above remain historical observations of that copy, not proof that every remote file had been present. This checkpoint instead starts from every one of the694 remotely verified f96 blobs and file modes, plus the single-document parent repair3f719df23fc852720a3222a5a55b137c661f8e2c. No existing remote file was omitted. Source changes relative to that exact parent are exactly29 files: the previous28 feature/build files and the contour-flow-review compilation dependency.

Fresh Release HEADLESS_WORKFLOW/QT_CANVAS_TESTS configuration with explicit primary Python3 completed its build and all110/110 CTest cases in45.98s, including actual Qt software canvas26.21s. Its ten pre-existing bounds Binding-loop warnings remain; the separately queued mouse-guide change is not included and no zero-warning claim is made here.

All ten independent renderer outputs actually compiled and linked: five CI experimental outputs, three artwork README examples, and contour-flow E02/evidence executables. Matching suites passed: automatic15/15 (3.047s), flow15/15 (1.965s), hybrid15/15 (3.720s), locality20/20 (0.529s); E02 reported18 cases and0 failures. The five documented illustration/evidence tools were compiled only, except that the E02 test executable was run. No drawing/evidence tool was run to generate an image. The dynamic source-list guard passed10/10 against this complete tree.

The contour document retains the parent's latest complete-tree validation note and adds only the ContinuousEraserCoverage dependency. Unrelated bytes and file modes are preserved. This is verified local source/build evidence, not a completed remote workflow, a Windows or physical-pen qualification, or an artist-quality output.


## Inventory-order repair parent revalidation

Reassembled on inventory repair 42f6f025 (tree 1c8fca1609c878ab5231d26a6d2c2c36657f8d23), retaining the full 695-file parent manifest plus eight eraser source additions (703 files). All B43 and build regression registrations precede the final TestTargets include. Fresh Release HEADLESS/QT_CANVAS build and full CTest passed **111/111 in 103.42 s**, including the real Qt software canvas test (22.25 s) and actual inventory-vs-CTest equality, including the regression itself. Final Qt log contains no Binding loop, TypeError or ReferenceError.

No manual native source list or renderer source changed relative to exact-eraser-work: its ten actual manual renderer links, four experimental suites (65 tests), and E02 (18 tests) are reused evidence, **not claimed rerun in this reparenting**. Windows/tablet hardware and artistic quality remain unverified by these checks.
