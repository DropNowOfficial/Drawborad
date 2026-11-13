# Live native ink feedback (local WIP)

Problem: while drawing the browser displayed every brush as one blue CSS-pixel line, hiding the selected width, color and pen pressure until release.

Chosen implementation: fine/smooth ink only, temporary viewport overlay, frozen stroke color/width, width × pressure / 2 radius at current zoom. Redraw full path once per animation frame so changing total-length mouse taper can update earlier portions. Interpolated taper ramp knots preserve two-point strokes. Single filled path prevents repeated translucent overlap accumulation. Cancellation cancels scheduled frame and identity-check rejects stale callbacks. Existing native command and saved geometry unchanged. Dry/MyPaint keep blue trajectory instead of fabricated texture.

Non-goals/limitations: not the final native rasterizer; browser AA differs especially for subpixel lines and tight turns. Overlay is topmost and does not represent layer opacity, occlusion or masks. Taper shape changes while length grows. No artistic quality or restoration claim. UI explicitly discloses these restrictions.

Validation: 11 UI regression scripts pass. New tests check zoom, width, pressure, taper knots, unchanged dots/input, actual draft coordinate placement and color, stale-frame rejection. Real browser/visual inspection pending shared CI path; not yet published.
