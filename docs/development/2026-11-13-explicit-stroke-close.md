# Explicit small-gap closure for hand-drawn ink

## Actual workflow obstruction
The existing closed-ink fill intentionally requires equal first/last coordinates. Ordinary desktop mouse/pen release merely appends its current sample; it does not guarantee exact closure. Previously tested closed shapes could satisfy this by precisely authored event coordinates. That is not evidence an ordinary hand gesture is conveniently fillable.

This adds a deliberate selected-stroke action, never automatic snapping: preview a straight connection from the last sample to the first, then accept or cancel. All original samples remain exact. One **synthetic** endpoint copies the first sample with the existing end timestamp; the existing event's other metadata and all later strokes remain unchanged. The endpoint is not claimed to be new physical-pen input. Joining endpoint caps can change rendered pixels.

## Boundaries
- Only eligible editable continuous-solid-ink strokes, respecting existing later-fill dependency barriers and recorded-source protection.
- Original raw count3..4095, temporary polygon3..256 distinct consecutive positions. Duplicate release positions are collapsed only for validation, never removed from the actual event.
- Nonzero endpoint gap no more than32 source pixels and no more than5% of original path length.
- Exact polygon validation rejects self-intersection, touch/backtracking, degeneracy and out-of-canvas geometry. No simplifying, distant closure or invented larger structure.
- CommandBus independently checks the same pure geometry rule and only accepts a single close operation. Target-node marking invalidates rendering caches. Undo restores the original open event.
- Closure does not promise that thin/transparent/zero-pressure ink meets the separate closed-fill rules. It does not modify the imported source image or repair artwork automatically.

## Verification
Core tests cover retained raw/release samples and later independent marks; Undo/native reopen; actual subsequent closed-ink fill; large gap, self-cross/backtrack, out-of-bounds, sample budget, locked layer and dependent fill refusal; zero-pressure closure followed by fill refusal. Final local Release107/107 passed19.96s, including actual Qt/QML18.74s. The mouse press/move/release creates a genuinely near-open loop, then the explicit button previews/cancels/accepts closure, Undo/reopen retains it, and the existing closed-fill workflow succeeds. Added direct-core total1M-point refusal after independent review found an append-budget gap; Qt rendering already rejected that size but core now independently refuses before any mutation. No artwork-quality or Windows/physical-pen claim.
