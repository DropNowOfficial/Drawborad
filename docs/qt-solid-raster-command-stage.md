# Solid raster acceptance command (WIP)

## A durable, limited alternative to a new vector format

The command builder turns one explicitly authored six-control-point solid shape into an independent transparent Normal raster layer, using the existing cropped raster storage container. It does not add engine25, change legacy replay, or save editable solid-vector semantics. Once accepted, the layer contains native pixels; its Bezier handles are not an editable persisted object. The user can undo the acceptance within the active history, or create a replacement. Reopening a file is not a promise that the prior undo stack survives.

The historical container stage contains the word `mypaint`, but this generator is not MyPaint. The actual layer name and source manifest identify Qt solid rasterization and baked pixels. The MyPaint-specific import/manifest checks are not bypassed or supplied with fabricated dab evidence.

## Preservation and geometry

Prepare the mask at the original canvas dimensions, scan its full native extent for the nonzero bounding box, and build transparent RGBA only for that entire box. The512px adapter tiles are internal processing units, not a crop of the accepted shape. Never rescale a too-large shape or silently clip it to a512px region. Empty/fully transparent shapes and over-budget rectangles are explicitly refused.

The first version is RGB8 with alpha and no ICC profile. It preserves the existing encoded-channel blend semantics, not linear-light or printer color matching. The background is transparent when generating the new layer; pre-existing artwork must not be baked into it. The existing document is applied only to a private candidate for validation. The returned command must still be accepted against its original revision by the application, and acceptance must remain one atomic history operation.

## Existing budgets and portability

Retain the existing16MiB raw-layer limit, including the96-byte header, the PNG/manifest limits, and the document's existing resource ceiling. A large canvas does not mean every possible full-page shape fits one cropped layer. Exceeding the cap is a refusal, not permission to raise reader limits or resize the artwork. The mask/image/encoder temporaries require their own bounded planning; no image-payload number is a total-process memory guarantee.

The generated PNG is decoded and checked against exact RGBA, dimensions and profile. Its manifest records actual backend/runtime, authored controls, canvas size, native ROI, color and a pixel hash, and states that the result is baked/noneditable. These metadata do not ask old readers to rerasterize the controls. All pixels needed for the layer reside in the existing resource structure, so old compatible readers need not have the new Qt mask backend.

## What remains

The builder and tests alone do not expose an accept button in the desktop. Product UI must present the actual candidate at the correct layer position, identify it as an independent pixel layer, preserve cancellation and stale-request protections, and require explicit acceptance. Keeping a separate temporary shape observation view is not equivalent to showing the final layered result. Windows, pen hardware, physical print output and artwork quality remain separate qualifications.

## Local acceptance evidence

A synthetic, manually specified six-point fixture produced an actual670×911 ROI at(45,39) on a1024×1024 canvas, rather than clipping to512 or resizing. Its alpha range0–143, including167,246 transparent pixels, confirms that the white document background was not baked into the layer. Existing blue-layer nodes/resources were unchanged. Acceptance, active-session Undo/Redo, real ProjectStore save/recover and native-layer reads passed.

The resulting DRAW-DELTA-14 file was opened with a binary built before the Qt mask backend existed. Its PNG and ORA merged pixels and the individual solid-layer ROI pixels matched the generator exactly. A separately linked verifier using old portable archives had no Qt dynamic dependency; it recovered, rendered all three layers, saved a copy and recovered that copy with identical content/pixels. This is evidence for this fixture, not a general Windows/platform qualification. ORA reported that layer locks are not preserved. Extra upper-occluder and hidden-layer fixtures were not specifically exercised here.

Only explicitly authored control points supply the shape. No reference image is read or copied into these generated pixels. A reference may guide a person's drawing, but must not be presented as newly hand-drawn artwork by reusing its pixels. These synthetic tests are technical checks, not a finished user illustration.
