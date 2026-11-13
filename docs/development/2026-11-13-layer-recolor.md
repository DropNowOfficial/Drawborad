# Native whole-layer recolor checkpoint

Goal: keep artwork editable after filling closed lineart regions. This increment allows a selected editable native color/ink layer to change color as one undoable action, without flood-filling it again. Raster/MyPaint layers remain unsupported, not silently replaced or claimed as vector artwork.

Implemented: `recolor_layer` native command, capability reporting, HTTP transport and editor button. Each existing event keeps its geometry and alpha. Layer visibility/writability and stale revisions are enforced. One Undo restores all previous colors; save/reopen preserves the result.

Verified locally on 2026-11-13: 87/87 CTest, 70/70 cloud Python tests with a real native engine (23 subtests), plus the synthetic native leaf demo. The new browser handler test uses a DOM stub, not Chromium or Windows acceptance. The MyPaint UI test fixture now imports the new real capability helper; it previously failed because its extracted-function VM omitted that helper. New tests are registered before CTest labels are collected.

The native demo starts from drawn closed outlines and bucket-fills two blank regions on a separate color layer, then recolors that layer. It uses no private references. This demonstrates editing, not artistic quality. The complete approved cover/comic and natural figure/brush quality goals remain unfinished. Remote CI must be checked on the exact published commit; local results do not imply remote success.
