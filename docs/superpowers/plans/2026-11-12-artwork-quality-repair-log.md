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
- A strengthened fixture places native strokes in all 66 layers. Latest report: five preview tests passed; protocol PNG approximately 1.285 seconds, 20,312 KiB RSS, `sourceAssetReads=0`. These are software fixtures, not delivered comic artwork.

### Build status and review

- Latest complete recorded CTest run: **52 of 54 targets passed**.
- C03 and C06 failed with JPEG/TIFF/WebP development libraries reported NOTFOUND. They remain failures, not passes or waived requirements.
- Independent review found that the new preview path could miss rejection of an unsupported tool when all layers were hidden. At this snapshot the repair was undergoing RED-to-GREEN validation; **do not mark resolved without the subsequent test result**.
- No new remote CI pass, production release, complete desktop installer, or all-platform acceptance is claimed.

## Full-page investigation: not a completed implementation

- Full six-page source census: approximately 7,654,966 contour segments, with 27–28 over-limit events per page. The complete sixth page does not fit current v3 per-event/total geometry budgets.
- Simple contour clipping was rejected: native C++ rendering demonstrated a 1/16 coverage difference/seam.
- An isolated original-edge clipping plus cross-chunk parity experiment rendered page six at 512 × 512 with 16 tones, matching its original-edge oracle. Reported work: 730,161 points and about 90.7 million work units, below a 128 Mi-unit cap.
- This remains a **lossy 16-tone research experiment**, not a production native.draw format, not a completed v4 primitive, not full-resolution restoration, and not an accepted comic page.
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

1. Finish unsupported-tool rejection regression and rerun preview safety tests.
2. Resolve codec dependencies and rerun all 54 native test targets; report actual counts.
3. Review full-page parity/coverage experiments against source crops and error images before choosing a production design. Do not ship the known seamed clipping approach.
4. Improve semantic masks and inspect faces, hair, panel borders, and background at native size; preserve rejected versions for comparison.
5. Produce a small representative native editable example, inspect it visually, then repeat at the required page size. Expand to six pages only after quality and resource gates pass.
6. Repeat GUI select/hide/recolor/undo/export/reopen on nonempty, representative layers; verify saved native content and exported dimensions.
7. Only then prepare reviewed implementation commits. Log publication is not code publication or artistic acceptance.
