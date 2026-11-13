# Solid draft preview: compatibility seam (WIP)

This step connects an explicitly temporary solid-mask draft to the existing tile color compositor. It is NOT a new saved event, a completed editable solid layer, or a delivered illustration. The optional Qt backend remains OFF by default. Original engine 1–24 replay, project files, PNG/ORA export and portable tool acceptance do not change.

## Contract

Use native geometric alpha from a specified published mask generation. Convert Alpha8 to U16 coverage exactly with multiplication by 257, then apply the existing `blendCoverage` routine to a candidate tile. Qt does not perform document-color composition. Input layer pixels remain unchanged on success, error and cancellation; callers receive a new result only after the complete requested draft succeeds.

The adapter accepts only validated RGB U8/U16 tiles with alpha and exact matching profile identifiers. It does not convert a differently tagged color, accept CMYK as RGB, or infer a missing profile. Values, layout and requested region are checked before compositing. Source color alpha and event opacity are applied once by the existing compositor; geometry masks never encode paint color or opacity. Recoloring the draft therefore must not rerasterize its geometry.

The product uses up to 512px render tiles whereas the bounded mask reader uses up to 256px reads. The adapter subdivides reads using the same global coordinates and authoritative native masks; it must not independently repaint each crop. Each successful result follows the caller's ordered draft list. Failed/stale/missing masks are errors, not transparent substitutes.

## Color honesty

Existing PixelBlend quantizes channels and coverage to U16, but it blends encoded channel values without sRGB-to-linear decoding. This adapter deliberately preserves that historical behavior. A black/white half-opacity example tests the encoded-channel result; it is not evidence of linear-light correctness, colorimetric rendering or printer matching. Likewise RGB16 intermediate support does not make the existing RGB8 merged-document pipeline an end-to-end 16-bit export path.

## Smallest useful desktop step (not yet connected here)

A clearly labeled temporary solid-preview toggle for an existing closed two-span six-control-point engine24 curve can help users adjust silhouette and negative space. It must say that the fill is not saved or exported; accepting controls still saves only the existing contour event. Unsupported spans/open curves, missing backend, budget errors or stale results must produce an explicit message and never display an older draft as current. Cancel, focus loss and document changes must retain current draft safety rules.

## Before durable solid artwork

A new format must define backend/AA/fill-rule/coverage policies and a portable appearance contract. The Qt runtime string alone does not ensure equal pixels across platforms/builds. Load, render, journal/backup, Save As, worker commands, hidden-layer export, undo/edit dependencies and Qt-free tools all need coherent preflight behavior. Unsupported readers must reject the whole new document before any blank export is possible. Until those paths are implemented and verified, temporary masks must never enter persisted project events.

Cancellation checks occur before work, between mask chunks/events, and before returning a successful candidate. The deterministic test in this stage covers a pre-requested cancellation; it does not claim to have experimentally forced every mid-composition cancellation timing. Empty paint lists are rejected rather than using them to bypass mask-domain validation. At most 4 MiB of input tile storage (including padding) is copied, plus bounded coverage/chunk buffers; this is not the whole application's memory budget.

The caller must supply background pixels corresponding to the requested global region and the correct target layer/composition stage. TileData itself carries no global origin or layer identity, so matching layout/profile is not proof of correct placement. Product integration must bind these facts in its immutable request. Placing this result on top of an already merged document would be incorrect when higher layers should occlude the shape; this adapter does not authorize that shortcut.
