# Ordinary lineart regions and hollow interiors in one workflow

## Real workflow gap
The previous controlled hollow path required an empty lower color layer. A user could fill an ordinary white region first, but then hollowing was rejected; after hollowing, the bucket also refused ordinary original-white areas. This broke the intended one-region-at-a-time workflow even though each isolated feature passed.

## Bounded connection
The lower layer may now contain validated native bucket events strictly inside original white/transparent pixels. Original grayscale/black ink is not an allowed lower domain. Upper color remains restricted to the union of accepted white-cover interiors. Cover geometry and original source resources are unchanged.

The bucket chooses the target by the clicked source domain, not the currently colored composite: accepted hollow interior uses the upper layer and controlled derived reference; original white/transparent region uses the lower layer and original-only reference. A lower-domain fill does not create an unused upper layer. Unhollowed black/gray seeds and incompatible manual brush/complex-layer edits remain rejected.

First hollowing validates existing lower events as well; later independent covers preserve both existing color domains. Candidate preview/accept/cancel, actual painted-layer selection, Undo and native reopen keep their prior contracts. UI states explicitly whether acceptance writes above or below the original lineart.

## Acceptance
Core tests cover valid domain mixing and reject malformed/cross-domain lower paint. Actual Main.qml exercises ordinary fill first, hollow/Undo/redo, ordinary fill again without creating upper, hollow-interior fill, exact region/boundary colors, source equality and save/reopen/cancel. Results follow after execution.

This is an integration repair, not new artwork, color-reference reconstruction or a claim of artist-quality completion. No private images are committed. Windows/physical input remain unverified; opaque white-paper and compatible stack constraints still apply to hollowing.

## Executed evidence
- Debug B33/B34 core regressions: 2/2 passed, 0.06 s.
- Release actual Main.qml regression: 1/1 passed, 16.58 s, including the entire mixed lower/upper workflow and domain-specific notices.
- Existing private 1024×1536 processed mask exercised via the real QtBridge: ordinary face-region fill before hollowing, accepted hair-interior fill afterward, original resources unchanged, Undo/save/reopen and renewed ordinary-fill review/cancel. Independent connected-component comparison found exactly 26,907 original-white pixels newly colored relative to the prior hollow/interior-only output; all other pixels matched. Visually inspected locally; not uploaded, not a new complete artwork.
- Remaining practical issue observed: gaps already present in processed lineart can join intended areas. This increment does not repair those gaps or claim semantic-region correctness.
