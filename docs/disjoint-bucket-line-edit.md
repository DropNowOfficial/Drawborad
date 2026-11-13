# Bounded line editing after frozen bucket fill

This WIP removes the blanket barrier that made all earlier native lines uneditable after a later bucket operation. It does not automatically refill or relax editing near filled boundaries.

For smooth ink (19), cubic ink (23), and connected cubic paths (24), both the old and candidate whole-event bounds must be strictly separated from every later native bucket run. Bounds include half stroke width plus 2 pixels; bucket runs also carry a conservative 2-pixel margin. Touching rejects. Control-hull bounds intentionally over-reject curved strokes.

The exception is restricted to plain reconstructed, normal-blend, untransformed, unmasked documents with no recorded fill operations. Unknown dependent events remain barriers. Hidden buckets are still checked. Existing no-bucket behavior and the generic independent-stroke classification remain unchanged. Builders and raw CommandBus entry points both enforce candidate geometry checks.

This enables an unrelated distant hand crease or contour correction after coloring elsewhere. It does not enable changing the outline of the colored shape, recover historical fill provenance, or guarantee arbitrary imported-project editability.

Regression coverage includes engines 19/23/24, raw-operation attempts to move into fill, old footprint touching, multiple fills, mask/transform/import/blend/recorded-fill guards, preservation of bucket events, Undo/Redo, and Save/Recover. Guard-only synthetic snapshots are not claims of valid recorded-fill authoring. Cloud native-render regional work verifies distant edits after creating lines then a lower-layer bucket, but is neither a GUI acceptance nor finished artwork.

Desktop menu eligibility now crosses validated bucket events and rechecks each candidate against the conservative core guard. The normal no-bucket cached fast path is preserved. After a bucket, at most128 recent smooth-ink candidates are examined per cache rebuild, so this bounded menu may omit earlier eligible lines. It does not promise exhaustive discovery in a large document.

## Acceptance checkpoint

The dedicated Linux software Qt/QML test first failed because `refreshEditCache` still stopped at every bucket even when core editing was permitted. This real UI integration defect was fixed; passing core tests alone did not establish user-visible functionality.

`Desktop.PostBucketEdit` now exercises synthetic line-then-bucket history through QML controls and point hit-testing, preview/cancel/reselect/accept, preserved bucket content, Undo/Redo, exact save/reopen rendering, and rejection of a candidate moved into fill. Three consecutive runs passed in0.64/0.77/0.63 seconds. Related B40/B49 cases passed3/3. Existing selection-cache getter fixture passed, but is not a rendering frame-rate or large-artwork performance claim.

No reference images or private artwork are part of this checkpoint. Physical stylus/Windows acceptance, unrestricted boundary edits, automatic refill, and finished high-quality artwork remain unverified or incomplete. Remote CI is not claimed green; no CI retry, payment, quota change, or merge accompanies this WIP checkpoint.

Final complete Release rebuild and136/136 local CTest passed in71.14 seconds after the Qt integration fix.
