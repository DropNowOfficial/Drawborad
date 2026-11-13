# Future MyPaint stroke folders (non-flattening)

Problem: each MyPaint stroke is an original locked cropped raster layer with its own manifest. Long painting sessions overwhelm the flat sidebar. Baking these into one image would lose per-stroke masks and undo granularity.

Implement an explicit new neutral top-level folder for future strokes only. No existing layers are moved, unlocked, resampled or merged. New cropped AddNode operations target the folder directly; all source bytes/manifest/resource binding and the original stroke lock remain intact. The root's last child must be this visible unlocked folder with Normal blend, opacity exactly 1, no masks, transforms, resources, semantic object or content. Core rechecks immediately before append. The new folder is just a structural group and does not introduce offscreen/isolated compositing; children still composite in their original order against the common background.

Only a qualifying topmost folder accepts new strokes. A selected destination that becomes invalid blocks painting instead of silently routing to root. User can explicitly select canvas top level or create another folder. Folder opacity edits other than 100% are rejected; rename, show/hide and lock remain available. UI folding changes presentation only. Individual MyPaint stroke masks remain editable under the existing bounded-mask workflow.

Real pinned libmypaint + native-engine integration compared three overlapping pressure-varying colored strokes (Multiply, Normal, Multiply) over a colored native background: grouped and ungrouped PNGs are byte-identical. Group child count/locks, per-stroke undo/redo, independent mask enable/restore, folder hide/show, Save separate copy/reopen and obsolete-folder rejection are tested. Real strokes, not image-generation output or a fake renderer. This is a compositing/control test, not a finished-art quality claim.

The existing packaged Chromium acceptance is extended to create/select the folder, draw two actual MyPaint strokes, expand/collapse and confirm pixels unchanged, then export/reopen. CI result is tracked separately. Headless/API/UI full suites run locally.

Limitations: folders do not reduce actual layer count, resource budget or rendering cost; no general arbitrary layer grouping, pass-through/isolation switch, group opacity or wet-pigment behavior is added. Artwork quality and texture richness are not declared solved.

Additional pre-publication regression: all socket-free UI scripts, including the three legacy credential/selection VM harnesses, pass with controlled periodic timers. The reconnect DOM stub now implements option creation/append required by the folder destination selector. Production auto recovery timer remains enabled.
