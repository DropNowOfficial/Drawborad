# Desktop selected-ink hollow preview

## Connected path
- New-document dialog explicitly offers opaque white paper; existing three-argument API calls keep their prior transparent default. No existing transparent document is silently converted.
- Mouse-only "selected ink hollow" tool is mutually exclusive with the fill bucket. Tool switches cancel an uncommitted brush gesture. Tablet input does not accidentally paint while this mouse-only mode is selected.
- Click selects a connected ink region in the declared original grayscale lineart, not the composite artwork. White-paper clicks, colored source, non-neutral roots and unavailable source stacks fail without mutation.
- Background computation builds the validated hollow command and temporary candidate. The existing actual document compositor shows an explicit uncommitted preview; accept/cancel reuse source-generation/revision guards and save/export/close protections.
- The operation adds an editable white-paper overlay while retaining the original resource. It is not a destructive edit of the original or transparency erasure.

## Verified
Actual Main.qml loaded in a Qt software window: the bound tool toggle, mouse canvas click, preview label and accept/cancel controls exercise this path. Committed source equality before accept, white cleared interior with black original boundary and an untouched independent fine mark, cancelled preview, accepted candidate equality, resource identity, Undo and save/reopen are checked. No-safe-interior thin marks remain unchanged with an explanation. Existing canvas/fill/export regressions also run. Release software test passed (15.20s before final text-only wording); focused Debug verification recorded with the published receipt.

This uses real Qt/QML objects and canvas events; it is not a Windows/physical-device or native file-dialog click acceptance. The earlier private old-mask exercise establishes a local operation on an old processed reference, not a newly completed painting or restored high-resolution detail.

## Honest remaining limits
This first desktop hollow operation allows one overlay. Repeating explicitly asks for Undo/reselection instead of silently widening stack support. The original-lineart bucket still uses the immutable original and its strict two-layer contract; it does **not** yet fill newly hollowed overlay regions in this same stack. A reviewed derived-lineart reference or controlled mask binding is needed to connect that next step safely. Do not advertise the entire hollow-then-color workflow as finished.

Source classification remains declared grayscale, not automatic faithful segmentation of the original colored art. No whole-page extraction, centreline thinning or generative image model is used here. Fine-detail choices, richer regional selection, repeated operations and complete artwork remain unfinished. Private images are not uploaded. Continue timely stage/blocker reports and distinguish old assets, new edits and new artwork in deliveries.
