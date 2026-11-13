# Reversible native layer ordering

Missing workflow: the editor could create paint layers but offered no control to place paint above/below existing ink. Adding more brush options does not solve this.

Expose the existing core MoveNode as a narrowly-scoped `reorder_layer` command: selected leaf Layer, same parent, exactly one sibling position, direction up/down. Native child order is back-to-front, so up increases the index. Cannot move roots/groups or cross parents. Layer/ancestor locks remain enforced by the core; the user must explicitly unlock locked layers. Moving a layer is different from editing its pixel resource. Candidate apply + merged-render preflight occurs before committing. One undo restores the order. No format change, no flattening, no stroke-event rewrite.

UI guards capability, selected kind, child count, ancestor locks, sibling edges and busy state. Labels describe compositing order, not changing original strokes. Hidden layers can be reordered without making them visible.

Synthetic native test: overlapping red/blue strokes change visible center pixel when order changes, retain event data exactly, undo/redo and save/reopen preserve expected image. Invalid direction, locked layer and stale API commands reject. API rejects attempts to add an alternate parent. UI tests cover old engine, locks, busy and top/bottom edge guards. Complete suite and independent review status are recorded in the follow-up receipt; not an artwork-quality claim.
