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
