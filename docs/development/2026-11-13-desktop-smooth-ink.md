# Desktop continuous solid ink

## Concrete quality gap
The desktop ordinary input path still used legacy engine2 even though continuous antialiased engine19 already existed. A same-path technical comparison was generated independently (121 pressure samples, widths 2/12/48, transparent alpha, crossing x512). Legacy2 visibly showed repetitive grain/grid and beaded narrow tips. Engine19 removed those artifacts and made continuous solid contours; dry12 instead produced longitudinal bristle channels. These are technical diagnostic strokes, not new artwork or a source-faithful cover. No diagnostic/private images are uploaded with this change.

## Implementation
An explicit continuous-solid-ink switch selects the existing engine19 helper. It is mutually exclusive with dry ink and leaves legacy mode available. Mode/UUID/color/opacity/width/pressure/taper are frozen per gesture. Preview and release compile the same event identity. Hardness is fixed by the engine and disabled in the UI; width <=128 native pixels and <=4096 raw input samples are enforced. Eraser intentionally retains the old eraser path.

New solid strokes use asynchronous candidate-render-before-commit, including strict full mixed-stack validation when targeting a finishing layer. Pending save/edit/export/replay guards and cancellation are reused. Mode changes cancel incomplete or still-validating strokes rather than silently changing their renderer. Existing saved stroke engines and old pixels are never rewritten. Persisted engine19/format19 remains explicit; this does not bypass old-reader rejection rules.

## Acceptance
Reviewed Release full suite: 105/105 passed (-j4, 30.73 s wall time), actual Qt/QML 18.46 s. Actual Qt/QML checks cover mutually exclusive mode controls, preview/commit pixel equality, source alpha and style opacity, synthetic pen pressure/time intervals, switch-cancel before write, save/reopen snapshot and render equality, and engine19 strokes used as explicitly selected fill boundaries. Review also found and fixed mode-switch checked bindings being lost after a rejected enable; the test verifies rejection at width256 followed by successful mutual switching with displayed state matching the active mode.

Independent diagnostic rendered alpha max143 matched source alpha0.8 × opacity0.7. This proves that case only, not color-managed print accuracy. Smooth ink is a uniform digital round brush, not arbitrary bristle behavior, intelligent shape reconstruction or complete artwork. The reference still needs deliberate shape, typography, composition and varied hand-authored marks. Windows/physical pen remain untested.
