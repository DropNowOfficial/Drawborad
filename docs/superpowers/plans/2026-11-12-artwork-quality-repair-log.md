# Drawborad artwork-quality repair: goal, requirements, and evidence log

**Snapshot:** 2026-11-12 UTC. **Status: experimental repair in progress, not production-ready.**

本日志区分需求、隔离环境中已实现并测试的功能，以及尚未完成的艺术质量目标。代码测试通过不等于画面达到要求；本次提交只同步日志，不发布修复代码。

## Scope and baseline

- Repository: DropNowOfficial/Drawborad.
- Baseline: [draft PR #7](https://github.com/DropNowOfficial/Drawborad/pull/7), commit `f9085f9934b5f0c2dba42b0433ecde15c862a794`, branch `work/serverless-generation-20261111`.
- Existing execution ledger: `docs/superpowers/plans/2026-11-11-serverless-generation.md` on that baseline.
- Repairs below are in an isolated cloud working copy. They have **not** been pushed as implementation commits, merged, released, or installed on the user's computer.
- This documentation-only branch does not imply that main or PR #7 contains the repairs described below.
- Source of this snapshot: implementation/test records and current repair-review findings. Later experiments require a subsequent log entry.

## Goal

Make Drawborad capable of producing high-quality, genuinely editable layered artwork that faithfully preserves the reference's composition, proportions, expressions, fine contours, and varied marks. Professional-artist-level quality is an acceptance goal, **not an achieved capability or guarantee**.

The immediate use case includes a cover and six comic pages. Native high-resolution work, semantic editability, and visible quality must all be evaluated separately. A larger exported pixel count alone is not restored detail.

## Requirements and acceptance criteria

1. **Reference fidelity:** retain faces, expression, hair, internal contours, hatching, and tonal boundaries. Reject coarse outlines, doubled edges, blurry regions, oversimplification, or substituted geometry.
2. **Coloring-outline mode:** hollow out filled regions while retaining their enclosing contours and useful internal details. Do not merely thin every mark or redesign the drawing.
3. **Real editability:** independently selectable and hideable objects, accurate person/background masks, editable child layers, undo/redo, and save/reopen persistence. Tonal layers are not automatically semantic layers.
4. **Native provenance:** reconstruct native editable geometry without presenting a flattened image wrapper, traced contour sequence, or generated test fixture as recovered human pen strokes.
5. **Resolution:** create and render at the required working dimensions. Upscaling a 512-pixel source does not establish new authentic high-resolution detail. Keep physical size, pixel dimensions, and DPI explicit.
6. **Print-size tracking:** current recorded targets distinguish a 5.5 × 8.5 inch comic body (1650 × 2550 at 300 DPI) from a poster of at least 11 × 17 inches (3300 × 5100 at 300 DPI). The stated 5.75 × 8.75 inch page and “0.25 inch each side” margin wording require reconciliation before final print acceptance; do not silently treat these as equivalent. Do not infer six pages from one page's dimensions.
7. **Workflow:** preserve originals and prior outputs; keep experiments separate; verify real GUI import/edit/export/reopen, not only internal API tests.
8. **Honest claims:** no guarantee of absent hidden provenance/watermarks merely because an output uses native geometry. No watermark-removal claim. Keep source-derived tracing distinct from new artwork creation.

## Implemented in the isolated repair copy

| Area | Implementation | Important boundary |
| --- | --- | --- |
| Native fill | Engine-v3 `vector-fill-evenodd` with compound contours, canonicalized reverse edges, cancellation checks | Filled geometry is not recovered manual brushwork; old readers do not gain v3 compatibility |
| Fill/mask validation | Check interior and boundary against fully reviewed masks; conservative grid checks; bounded work | A valid mask can still be artistically wrong |
| ROI quality gate | Objective reference-comparison gate integrated into reconstruction | Numeric similarity is not aesthetic acceptance |
| Layer budgets | Repair 33-layer preview/PSD retained-buffer accounting while retaining the 128 MiB allocation guard | Guard is not a whole-process RSS guarantee |
| Semantic split | Complementary foreground/background masks and two-object project generation; atomic paired-mask revision | Initial split creates a new project; mask is pending review, not accurate automatic segmentation |
| Object editing | Select object, scope child layers, hide/show, undo, and mutation protection during replay/busy state | Standalone native QtQuick shell has not been visually accepted |
| PNG preview | Single merged frame with tiled rendering instead of retaining every full-resolution layer | ORA/PSD still preserve layers and their memory limits; no silent flattening |

## Test and workflow evidence

### Native fill and reconstruction

- A face ROI test used 33 tonal events/layers on a 960 × 560 canvas derived from a 240 × 140 source ROI. Render was approximately 2.19 seconds at 47 MiB RSS; native file approximately 9.5 MiB.
- This demonstrates native filled-contour execution, **not new source detail or finished artwork**. Complexity remains excessive for a small ROI.
- Fill/mask safety suite: 15 cases, including interior containment and tiny/sliver boundary cases.
- Native v3 event limit remains 16,384 vertices; total geometry and validation budgets are enforced. Prior v2 files must not be overwritten on a claim of universal compatibility.

### Layer persistence and export

- 33-layer preview/PSD retained-buffer accounting measured 92,360,704 bytes. A 133-layer case rejects before unsafe allocation.
- PSD test: 35 records (33 layers plus 2 groups), matching merged pixel hash; 77,421,020-byte PSD.
- Observed export RSS was 102,276 KiB; independent readback RSS was 175,720 KiB. The latter explicitly exceeds 128 MiB process memory.
- Complementary-mask split tests cover unchanged composite, visibility, recolor, persistence and undo. Atomic mask-revision tests cover paired updates and invalid/reviewed/locked/soft/stale cases.

### Actual GUI round trip

- Cloud desktop Chromium: imported a two-object, 66-layer project; hid the person object; undid; recolored the background; undid; saved/downloaded; reopened.
- Downloaded and reopened snapshots retained the same content (revision metadata advanced from 2 to 6).
- A separate 1536 × 1024, 66-layer software fixture exported PNG successfully. The initial fixture had only two painted crossing strokes and 64 empty layers, so it was not a representative dense comic.
- A strengthened fixture places native strokes in all 66 layers. Latest expanded suite: six preview tests passed, including pixel checks across tile boundaries, transparent gaps, and first-33-event replay. All 66 layers have indexed native events. Protocol PNG approximately 1.285 seconds, 20,312 KiB RSS, `sourceAssetReads=0`. These are software fixtures, not delivered comic artwork.

### Build status and review

- Latest complete recorded CTest run: **52 of 54 targets passed**.
- C03 and C06 failed with JPEG/TIFF/WebP development libraries reported NOTFOUND. They remain failures, not passes or waived requirements.
- Independent review found that the new preview path could miss rejection of an unsupported tool when all layers were hidden. The regression first failed, then passed after renderer preparation was moved before skipping hidden layers. All six preview cases now pass.
- No new remote CI pass, production release, complete desktop installer, or all-platform acceptance is claimed.

## Full-page investigation: not a completed implementation

- Full six-page source census: approximately 7,654,966 contour segments, with 27–28 over-limit events per page. The complete sixth page does not fit current v3 per-event/total geometry budgets.
- Simple contour clipping was rejected: native C++ rendering demonstrated a 1/16 coverage difference/seam.
- An isolated original-edge clipping plus cross-chunk parity experiment rendered page six at 512 × 512 with 16 tones, matching its original-edge oracle. Reported work: 730,161 points and about 90.7 million work units, below a 128 Mi-unit cap.
- This remains a **lossy 16-tone research experiment**, not a production native.draw format, not a completed v4 primitive, not full-resolution restoration, and not an accepted comic page.
- Visual comparison against the source **failed artistic acceptance**: quantization produced unnatural bright spots and stepped gray tones in the man's face/clothing and photographs. Do not promote this experiment as a completed high-quality page.
- Matching an experimental oracle only validates that experiment's rasterization, not similarity to the source or professional drawing quality.

## Unresolved / not implemented / not accepted

- Artist-quality reconstruction and faithful varied mark-making.
- Accurate semantic person/background segmentation: the current candidate mask still includes panel borders/background and has coarse edges.
- Native high-resolution reconstruction of all six complete pages.
- A production, reviewed format/primitive that safely represents full-page contour complexity without coverage seams.
- Reliable cross-version UX for opening v3 files in old readers; recovery fallback to older backup files must not be mistaken for successful new-file loading.
- Complete codec-enabled test run, independent final review, full GUI regression, and publishable implementation commits.
- Finished cover/comic artwork and print-ready acceptance.
- Any claim that unavailable source detail has been recovered, that hidden watermark absence was verified, or that generated fixtures equal an artist's layered work.

## Next work and exit gates

1. Retain the now-passing unsupported-tool rejection regression in the final integrated suite and independent review.
2. Resolve codec dependencies and rerun all 54 native test targets; report actual counts.
3. Review full-page parity/coverage experiments against source crops and error images before choosing a production design. Do not ship the known seamed clipping approach.
4. Improve semantic masks and inspect faces, hair, panel borders, and background at native size; preserve rejected versions for comparison.
5. Produce a small representative native editable example, inspect it visually, then repeat at the required page size. Expand to six pages only after quality and resource gates pass.
6. Repeat GUI select/hide/recolor/undo/export/reopen on nonempty, representative layers; verify saved native content and exported dimensions.
7. Only then prepare reviewed implementation commits. Log publication is not code publication or artistic acceptance.

## Follow-up: 2026-11-12 03:06 UTC

### Verified since the initial snapshot

- A complete rebuild and CTest rerun again produced **52 passes out of 54 targets** (15.98 seconds). This is not a fully passing suite.
- `V01.PaintedPreview`: all six cases pass, including the all-hidden unsupported-tool regression and the strengthened 66-nonempty-layer fixture.
- The stable repair patch still passes a clean applicability check against the recorded PR #7 baseline. Applicability is not production acceptance; the implementation remains unpublished.

### Codec failure clarification

- JPEG, TIFF, and WebP runtime support exists in the cloud environment; Pillow can handle all three formats.
- The missing components are C++ development headers/build-discovery configuration. CMake reports the optional dependencies as NOTFOUND, so the tested native binary lacks those codecs and C03/C06 fail.
- No dependencies were installed: the user's instruction to defer installation remains in effect.
- This limitation does not block PNG, native `.draw`, or the tested PSD/ORA paths. It is **not the cause of the artwork's visual-quality failures**.

### Still experimental / unverified

- A new v4 candidate is being integrated in isolation; it has not been merged or published and is not yet a production-format solution.
- No new accepted full-page artwork, six-page completion, semantic-mask acceptance, or professional-quality result is established by this update.
- This follow-up uploads documentation only. No repair code, experiment assets, private working directories, credentials, or internal tool paths are included.

### Additional visual diagnosis and GUI evidence

- Nine ROI comparisons isolated rasterization effects: pixel-center-sampled contours matched direct quantization with a maximum pixel difference of zero. Applying 4 × 4 area sampling to an already pixel-center-sampled source introduced additional softness.
- Averaging coverage independently for cumulative tone layers and then alpha-compositing loses subpixel correlation. Increasing from 16 to 64 tones alone can worsen dark borders rather than improve fidelity.
- An isolated alternative composites the tones at matching subpixel positions and averages once at the end. This reduced excess dark borders, but remained too soft: **artistic acceptance still failed**.
- Face, hand, and photograph crops were compared at 16/32/64 tones, including vertex costs. No higher-tone setting was accepted merely because its numeric count was larger.
- The page-six original-edge clipping/parity experiment used 730,161 original points and 90,735,409 work units and matched its original-edge oracle within budget. It is still not a production `.draw` artifact and remains visually unacceptable in the face, clothing, and photographs.
- The strengthened 66-nonempty-layer fixture was imported and displayed on the actual cloud desktop. The UI reported zero layers without strokes. This validates fixture handling, not artwork quality.
- Production-candidate v4 codec, mask, and regression work remains isolated and ongoing. No production-format acceptance or code publication is claimed.

## Follow-up: 2026-11-12 04:10 UTC

This section records later isolated-candidate evidence. It supersedes earlier "ongoing" statements only for the specific checks below, not the overall artistic acceptance status.

### Native v4 candidate and review

- Explicit clipping and original open-run metadata have been connected to codec, document/bounds handling, original-edge chunk parity, and reconstruction mask validation in isolation.
- Reported candidate checks: core 7/7; E01 20/20; prior regression selection 17/17. Cases include holes, endpoints outside the clip that need no review, and tiny/sub-ULP mask violations inside the clip.
- Actual v4 save/reopen preserved content. A stable v3 reader rejected the actual v4 file with `project.decode`, rather than pretending to load it.
- Read-only independent review had not identified a confirmed defect at the reported checkpoint; this is not an independent test rerun. A byte-level corrupt-`runStarts` decoder rejection case was added following review.
- Original limits remain in force. These are isolated candidate results, not merged implementation or a release.

### Failed fine-line reconstruction experiment

- A bounded single-straight-line coverage fit passed nine synthetic checks, including subpixel position/width recovery and rejection of crossings, texture, and unidentifiable cases.
- A real 12 × 12 neck-line crop lost natural variation: RMSE 9.94 and maximum error 53.66. It was explicitly rejected, not applied, and not expanded to the face.
- A lower-degree curved, variable-width model was investigated separately; no accepted reconstruction result is claimed here.
- Earlier corrected area sampling only slightly reduced dark borders: eyeglasses, fingers, hatching, and lip contours remained too soft.

### Co-development and PR #9 compatibility

- The separate Business-workspace PR #9 snapshot was inspected at head prefix `1a960390`. Its six new QA/documentation/CI paths had no path overlap with the stable repair's 39 paths or the then-current v4 candidate's 11 paths.
- No PR #9 source files were overwritten and no remote code was merged. Zero path overlap is not a guarantee of semantic compatibility.
- Its artwork-review tool was used on the native v4 page-six PNG and five important regions. The report was opened in real Chromium and exercised at 100%, 200%, then 100%.
- Diagnostic mean errors included whole image 7.526, father's face 8.630, and sister's hand 9.691. The face and whole-page rendering still appeared soft; these numbers did not establish quality acceptance.
- Local pytest was unavailable and was not installed. The official 13 tests were not claimed as locally rerun; the PR's remote CI results remain separate evidence.

### Genuine page-six editing, then conservative object separation

- Actual cloud GUI imported page six as v4: 1,041 events and 17 layers. Selecting event 701, recoloring green, undoing, and saving revision 3 worked visibly.
- That test also exposed a semantic limitation: one tonal event affected both the sister and the boy. Tonal strips are not person-specific layers.
- A conservative sister-interior mask excludes uncertain sofa/boundary regions. The candidate split prunes strips with no mask intersection, avoiding unconditional duplication beyond the one-million-vertex budget. The original project is retained; arbitrary expansion of the pruned masks is disallowed to prevent holes.
- The first split candidate had 2 objects, 34 layers, 1,277 events, and 944,151 vertices. Composite pixels matched the unsplit v4 result exactly; save/reopen matched.
- Recoloring the foreground changed 6,118 pixels, with zero changes outside its mask; undo was exact.
- Actual Chromium selection showed 236 filtered sister-interior events. Hiding it visibly removed only the reliable interior while leaving uncertain edges and other figures/panels; undo restored the image.
- Initial selected events 0/150/235 changed revision without an obvious visible difference, illustrating occlusion/no-effective-mask-intersection usability problems. This was not reported as successful visible editing.
- Filtered index 190 was subsequently verified headlessly to change 39 pixels and visibly changed a small green region at the girl's left hair tip in Chromium. Undo and Save restored the original. This proves a particular effective edit, not usability of every listed event.

### Source-grid sampling v5 and tonal candidates

- A source-center sampling experiment restored the source's eyeglass, finger, and lip contours more clearly than the old area-averaged rendering. Explicit source-grid sampling was isolated as a v5 candidate; ordinary v4 antialiasing behavior was kept separate.
- Uniform 16-tone source-grid output matched direct quantization, but retained quantization highlights. It remains derived from a 512 × 512 source, not new high-resolution detail.
- A Lloyd 16-tone native single-object candidate matched the independent diagnostic output at 829,529 vertices. Two-object variants required 1,038,941 vertices (1,037,818 after conservative coverage pruning) and were actually rejected. The one-million limit was not increased.
- A Lloyd 15-tone candidate fits: one object has 775,132 vertices, 998 events, and 16 layers; the two-object version has 971,403 vertices, 1,222 events, and 32 layers, including 224 foreground events.
- The 15-tone candidate saves/reopens exactly; its composite matches the single-object render and independent PNG. Foreground recoloring changes 6,118 pixels with zero mask leakage; undo is exact.
- The real cloud GUI imported and displayed the two-object 15-tone candidate with the stated counts. Visual review found it clearer than the old antialiased version and without the large highlights seen in uniform quantization. This is a source-fidelity candidate, not final artwork acceptance.

### Object/layer batch recoloring

- A candidate node-recolor operation changes all strokes under the selected object/layer in one transaction, preserving alpha and masks. It enforces a 4,096-event operation guard and atomic rejection when a child layer is locked.
- Native session and JavaScript selection tests passed. Actual legacy v1/v2 projects with 4,097 events were rejected without state/pixel mutation; partial-lock rejection also left state and pixels unchanged. An independent unlocked object remained recolorable and undoable.
- Real cloud GUI on the uniform v5, two-object, 34-layer candidate: one-click sister-interior recolor was visibly green while other figures and boundaries stayed unchanged; Undo, Save revision 4, actual native-file download, close, and reopen restored the original.
- This operation applies a flat chosen color: **it replaces shading and is not shading-preserving repainting**.

### Remaining acceptance gaps

- Accurate complete-person semantic layers, including uncertain boundaries and hidden content, are not complete.
- Source-grid fidelity improvements do not create new authentic high-resolution detail or human-artist-equivalent work.
- All six pages, final cover, print-ready acceptance, full standalone desktop acceptance, and an integrated all-green test run remain unestablished.
- Source/reference quality, semantic-mask approval, performance, compatibility, and artistic quality still require separate gates.
- Only this log is published by this update. No candidate implementation, artwork source, private path, or credentials are uploaded; no merge or release is performed.

## Follow-up: 2026-11-12 05:43 UTC

All implementation below remains isolated candidate work. This documentation update does not publish code or images, merge branches, or declare the requested artwork finished.

### Native 2048 creation and genuinely layered export

- Actual cloud GUI: created a 2048 × 2048 canvas, selected Paint, drew with the mouse, and obtained one native engine-v2 stroke with 369 samples. Undo/redo, save, PNG export, native-file download, close/reopen, and PNG re-export were exercised; reopened pixels matched exactly.
- Native input checks covered width, undo/redo, persistence, 513-point rejection, and locked-layer rejection. These are mouse/software checks, **not physical pressure-tablet validation**.
- Later actual GUI: created two layers and drew on each with engine-v6 clean ink; hid the second layer while the first remained; undid; saved revision 6; downloaded ORA, PNG, and native project.
- Independent inspection found two nonempty 2048-pixel layers and two native fineliner strokes. ORA recomposition matched every RGBA pixel of the downloaded PNG. Removing a layer changed 17,261 pixels, demonstrating nonflattened content.
- Closing and reopening the downloaded native project in the actual GUI retained revision 6 and both layers/strokes; a fresh PNG again matched every pixel.
- Streaming two-layer 2048 ORA tests reported approximately 55 MiB peak memory, exact mask/opacity/visibility composition, independent layer edits, and preservation of the prior destination on cancellation/failure. The original memory cap was not increased.
- These images are test strokes, not completed user artwork.

### Low-pressure gaps, clean ink, compatibility, and input aids

- A 74-stroke native 2048 diagnostic page exposed pressure-dependent gaps: at nominal width 24, sparse input left 530 of 780 columns blank at pressure 0.05 and 364 blank at 0.1; dense input had none. Nominal-radius spacing ignored pressure.
- Explicit engine-v6 spacing reduced these diagnostic blank-column counts to zero. Clean-ink selection avoids the observed repeated 8 × 8 grain pattern; it is not a claim of artist-quality mark-making.
- Work-budget validation now runs before commit in document/event-writer/render paths; malicious import/paint submissions were rejected without state mutation.
- Independent checks on actual engine-v2/v3/v4/v5 files found pre-v6/current rendering and save/reopen RGBA pixels identical. Four v6 low-pressure lines had no blank columns; 592 pressure/one-pixel partition comparisons matched.
- Selected native regression suite: **20 related CTest targets passed**, not the complete repository suite. Do not replace the earlier 52/54 whole-suite record with an all-green claim.
- A client-local reference overlay labels its source dimensions and stays out of exports. Actual GUI drew a new 417-sample stroke on 2048 pixels while viewing a 512-pixel reference; PNGs with reference enabled versus cleared were identical (2,030 nontransparent pixels). The source was not baked in or upscaled into fake paint.
- An optional lightweight stabilizer is explicit opt-in, off by default, and frozen at pointerdown. Latest reported frontend counts: 12 paint + 6 stabilizer + 4 reference cases = 22/22, with layer-selection regression passing separately. Actual GUI enabled it, drew 497 geometry samples, saved, and exported 2048 PNG; no physical stylus jitter study is claimed.
- Three-state mask UI keeps unknown separate from background: the tested source had 6,118 person pixels, 256,026 unknown pixels, and zero confirmed background. A five-pixel background edit and undo worked; a 4× view did not alter the underlying source grid.

### Artistic trials that were rejected

- Two original native 2048 character studies rendered and saved/reopened: v1 had 54 events, five layers, and 8,791 points; v2 had 57 events, five layers, and 7,197 points. They did not use a source bitmap wrapper or a deep generation model.
- Both implementation review and separate visual review rejected them. Fixing a contour passing through the face did not fix helmet-like hair, mismatched facial perspective, or mechanical hatching. The dominant failure was drawing planning, not pixel dimensions.
- An eye/hair study with three objects, four layers, 32 events, and 11,007 points passed actual GUI hide/undo pixel comparisons, but was also rejected artistically.
- Repetitive mechanical Bézier-art iterations were stopped. These failed studies are not uploaded as finished results.

### Licensed human-authored vector demonstrations, not original delivery

- A limited Open Peeps demonstration used Pablo Stanley's CC0 author paths, preserving eight paths as four native objects/eight layers. A 0.05-pixel approximation with 6,269 vertices passed bounded whole-image and 1:1 visual comparison. Real GUI eyepatch hiding changed 20,021 pixels; undo matched exactly.
- This demonstrates handling of an authored vector cartoon. It is **not original artwork, not fine-manga style acceptance, and not proof of full arbitrary-SVG support**.
- The more relevant Niabot “Figure in Manga style” source has an explicit 104-path lines layer. The extracted elements, attributes, and path data were checked for exact preservation. That layer uses solid black even-odd paths without transformations, clipping, gradients, filters, embedded images, or use references.
- Its CC BY-SA 3.0 attribution, modification notice, and same-license requirements are tracked with the demonstration. This is not a faithful import of the full colored illustration and makes no claim about the artist's underlying reference process.
- A counterexample exposed incorrect merging of same-endpoint move subpaths and lost implicit closure. Independent helper/native probes verified the subset-parser fix; this remains a restricted SVG subset.
- Neither third-party source images nor demonstration artwork are included in this log-only upload.

### Session recovery reliability

- Actual Chromium testing exposed lost project IDs/live-dirty recovery trouble. The previous 32-event revision-20 project was safely saved before repair.
- An authenticated, read-only active-project lookup avoids implicitly starting a session. Four actual HTTP checks plus engine regression passed; GUI reload recovered a dirty one-event project, with undo to zero and redo to one working.
- Restart without a live project produces an explicit no-live-state message rather than inventing recovery. This does not imply durable recovery from every crash mode.

### Current fine-line coverage and resource-accounting review

- An area-coverage prototype improved previously missing fine tips, but the first approximately 22.67-million estimate counted geometry work without all path work.
- Accounting was corrected: bounding-box charging is 28,930,998 work units; cumulative 16-pixel tile charging is 31,764,743.
- Independent review and production engine-v7 integration are **not complete**. Improved prototype appearance and corrected accounting do not establish final fidelity, safety approval, or production acceptance.

### Current bucket-fill candidate and compatibility limits

- A selectively ported engine-v8 bucket core builds. `B08.Bucket` passed six internal cases in approximately 0.12 seconds; the new frontend helper passed seven mocked cases.
- **Frontend integration and actual GUI bucket acceptance are not complete.** Mocked helper tests are not browser acceptance.
- Old engine-v2 bucket events do not automatically become compatible with the new engine-v8 representation.
- The proposed new bucket event uses scanline pairs with limits of 65,536 runs / 131,072 points, while retaining the document's one-million-point and 4,096-event limits.
- Masked targets or ancestors are rejected; no silent bypass of mask semantics is claimed.
- Actual Niabot coloring, save/reopen persistence, and rejection by an older reader are still being verified at this snapshot. They must not be described as passing yet.

### Current completion boundary

The software can now demonstrably create and round-trip native 2048 strokes and independent layers, but that does not complete high-quality layered character art. Fine-line fidelity, production v7/v8 safety and compatibility, actual bucket UI workflow, complete semantic editing, and artist-level artwork remain separate unfinished gates. No 512-to-2048 “detail restoration” or completed six-page delivery is claimed.

## Follow-up: 2026-11-12 06:36 UTC

This entry updates the bucket workflow and compatibility evidence. Candidate code remains isolated and unpublished.

### Selected native checks and work budgets

- Five selected targets passed in 5.62 seconds: `F04`, `H04.StreamingOra`, `B08.Bucket`, `V07.FineCoverage`, and `B09.FineFill`. This is **not a complete-repository test run**.
- Native engine-v7 2048 PNG charged 110,042,765 shared work units.
- Single-pass layered ORA charged 127,016,797 of 134,217,728 allowed work units. Merged pixels matched exactly, and separate empty Color / populated Ink layers were preserved.
- The native fine-v7 + bucket-v8 combined test charged 127,750,361 work units, with 1,880 fill runs and 215,613 filled pixels. All 104 ink events were unchanged and undo was exact.
- These cases fit without raising limits, but leave little budget headroom. They do not establish arbitrary-project capacity.
- Masked v7 operation remains rejected rather than claiming unverified support.

### Actual GUI bucket workflow, with a precise version boundary

- The real cloud browser completed an **engine-v3 authored-path ink + engine-v8 bucket** workflow: manually entered a skin-color hex value, clicked the face, and created one fill event.
- Saving to the old project path was rejected. Saving an independent new copy worked, followed by Undo, Redo, and Save at revision 4.
- Earlier Chromium error 9 was traced to GPU-unavailable browser logs. The test browser ran with GPU disabled; its sandbox was not disabled.
- This verifies the v3-ink/v8-fill GUI path. The **v7-fine-coverage/v8-fill combination has not yet passed actual GUI acceptance**, despite passing the native combined test above.
- This licensed-source coloring demonstration is not original artist-quality output or completion of the requested comic pages.

### Reproduced old-reader overwrite hazard and new safeguards

- An isolated temporary copy reproduced a serious compatibility hazard: an older reader fell back to a valid legacy backup and then Save overwrote the newer v8 primary file.
- The new Session/ProjectStore path holds a file lease and requires a clean, distinct file stem for version upgrades/downgrades. Mixed-version primary/backup/journal combinations are rejected.
- Rejection checks preserved original and backup bytes and active state unchanged.
- The new reader classifies unknown engines as `UnsupportedFeature`, does not fall back to the older backup, and retains the primary-file error.
- Actual HTTP save-copy checks rejected stale revisions, switched the active project to the new ID on success, and left downloads of the original project byte-identical.
- Two old-reader builds, pre-v6 and pre-v8, rejected the tested pure-v8 primary/backup pair.
- **Already released old readers are not repaired by this candidate.** Keeping new-format copies separate remains necessary. Do not describe this as universal backward compatibility or safe overwriting of legacy projects.

### Unfinished gates

- Actual v7 + v8 combined GUI workflow and its save/reopen/export acceptance.
- Independent review and broader regressions for the new compatibility protections and budget accounting.
- Support for currently rejected masked v7 cases, if introduced, would need its own design and tests.
- Artist-quality original creation, accurate complete semantic layers, and finished high-resolution comic delivery remain unachieved.
- Only documentation is uploaded in this update; no implementation, reference images, or candidate artwork is published.
