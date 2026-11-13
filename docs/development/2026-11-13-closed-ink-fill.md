# Explicit bounded closed-native-ink fill

## Motivation and scope
One rejected manually observed contour study exposed pale pixels between an antialiased native outline and its integer flood interior. A private same-outline technical experiment showed that geometric interior coverage could improve those pixels without redrawing the outline. The picture remained artistically unacceptable; no study/reference image is included here.

This adds a distinct whole-closed-path preview action, not a silent change to the existing seed-connected bucket and not a universal white-seam repair. It never automatically closes a gap or replaces an already accepted fill.

## Gates
- Explicit current editable finishing layer with exactly one engine19 solid-ink event, opaque color/opacity, positive pressure and effective width>=1 source pixel at every raw sample.
- Explicit exact closure,3..256 distinct temporary vertices; adjacent identical input positions (such as release duplicates) may be collapsed only in temporary geometry after validating every raw sample. No original event is rewritten. Other duplicates, backtracks, self-cross/touch and degenerate geometry are refused.
- Controlled source stack is the original two-layer form, without hollow/correction/barrier layers. The entire original source is white or transparent, so hidden internal dividers are not crossed. This restriction is intentional and shown in the UI.
- Fill uses the outline RGB. Four mutually exclusive2x2 coverage classes become at most4 independent-alpha bucket events on the lower color target. Any overlap with an existing lower fill, even translucent, is refused rather than overwritten.
- Bounded ROI×vertices×4 work, run/event/point limits, canvas limits, cancellation and full candidate validation/render apply before explicit accept. Undo, save/reopen and stale-result protections are retained.

## Preservation and limitations
Existing events, resources and layer properties are unchanged. Other layers' stored content and isolated render remain unchanged; their composite appearance through transparency can change when a lower color is introduced, which the preview/accept text discloses. Pixel-square geometric support is the relevant outside-region definition, not simply the pixel-center location. The2x2 polygon coverage and native stroke's distance-filter AA are different, so no universal seam-elimination claim is made.

## Pixel support rationale
For a2x2 subpixel sample inside a polygon while its pixel center is outside, center-to-boundary distance is at mostsqrt(.25²+.25²), about0.3536px. With the existing native stroke filter and effective width>=1px, that pixel already has outline coverage of at least about0.6464. Thus this quantized interior fill does not introduce an outside pixel that was previously completely blank; it may darken already antialiased boundary pixels. Independent tests also check polygon/pixel-square disjoint support, not merely the candidate mask. This is a technical support argument, not a blanket outside-center pixel-value equality claim.

## Acceptance
Local Release107/107 passed20.70s (-j4), including actual Qt/QML action, unchanged original pending preview, blocked save/layer switching, cancel/accept, Undo/redo, exact reopen pixels, open-loop refusal, repeated fill-overlap refusal and worker cancellation. B41 covers concave/reversed geometry, four disjoint levels, independently tested outside pixel-square support, other-layer isolated render equality, duplicate-release preservation, zero-pressure duplicate rejection, self-cross/transparent/partitioned-source/work-budget rejection and saved state. The technical fixture improved61 interior pixels relative to the existing flood fill; this is not a picture-quality score. Independent review found no blocking data-safety issue. No Windows/physical-pen or artwork-quality claim.
