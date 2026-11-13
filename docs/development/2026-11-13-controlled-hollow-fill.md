> Follow-up: `2026-11-13-multi-region-hollow.md` extends the original one-event limit to multiple disjoint accepted cover events; this document records the earlier checkpoint.

# Connecting accepted hollow regions to independent color

## Controlled derived reference
The bucket now has a restricted route for the accepted hollow stack: empty lower color layer, locked original lineart, one white overlay, and optionally one upper color layer. It reconstructs a temporary reference from original RGBA plus validated white overlay runs, never from already colored output. Color belongs above the white overlay so it is actually visible.

Validation uses content, not layer names or a tag claiming authorship: neutral root; white opaque paper; visible identity/unmasked layers; grayscale original; exactly one full-white native bucket cover event with legal integer runs, all strictly inside the original ink boundary. Existing upper color events must be compatible bucket runs contained in that accepted cover. New seeds must be inside the accepted white cover, avoiding accidental background fill. This validates compatible semantics, not provenance/authorship. Other edited or arbitrary stacks are explicitly rejected.

The first color layer plus paint is one atomic command; subsequent colors use that same independent layer. Both retain original resources and the white-overlay event. Saving/reopening needs no transient browser state or newly invented format: the validated original and native white runs already persist in the document. Subsequent sampling rebuilds the same derived reference and ignores existing color events.

## Desktop behavior and evidence
The normal bucket after accepting a hollow preview selects this controlled route. It still calculates off-thread, shows a real uncommitted candidate, requires accept/cancel, and honors source identity/revision and save/export/close guards. Successful acceptance selects the actual painted color layer.

Core B34 checks atomic Undo, repeated reference geometry, exact render color/black boundary, original resource/hash and overlay-event preservation, save/reopen, cancellation/budgets and malformed or incompatible layer/cover cases. Main.qml regression covers accepted hollow reopen, tool toggle and click, candidate/accept, independent upper layer, black boundary/fine-mark preservation, Undo removing the whole first color layer, redo/save/reopen, repeated color geometry and Undo.

An existing private source-derived ink mask was also exercised locally through the actual desktop controller. Independent connected-component/pixel comparison confirmed only the clicked hollow component was filled and other hollow interiors stayed white; Undo/save/reopen/cancel and source preservation were checked. No private image or identifying data is committed. This is a new operation on old processed material, not a new finished artwork, lossless reconstruction, or new native-resolution detail.

## Remaining
One accepted white overlay, restricted empty-bottom/locked-source/white-cover/upper-bucket stack; manual brush events or arbitrary masks/transforms in this stack are not silently accepted. More flexible multi-region authoring needs its own safe extension. Colored-reference fidelity, selective expressive details, professional stroke/material quality and complete artwork remain unmet. Import/application/save still have synchronous stages; scratch budgets exclude source/history/display. Windows and physical input unverified. Continue timely stage/blocker reports with explicit output provenance.
