# Desktop brush control gap: bounded implementation

Private local recovery imagery was viewed read-only to ground the audit. It needs
both fine contours and broad ink masses; some recovered edges still contain gaps,
ragged contours and unwanted remnants. No reference image or private content is
included in this commit. Parameter controls do not repair that art automatically.

The desktop previously forced every gesture to a 12px dark pencil, full opacity,
fixed hardness. It now exposes width (0.5–128px slider; API up to512), RGB color,
opacity, hardness and optional mouse end-taper in the actual QML interface. Settings
are captured at pen-down so changing a control mid-gesture only affects the next
stroke. RGB controls explicitly refuse CMYK painting without a conversion path.

Mouse taper reuses the bounded existing helper; it modifies pressure on the same
centerline, with minimum endpoint pressure0.05. It is assistance, not measured pen
pressure. Real tablet input bypasses it. The helper now has an opt-in timing-preserving mode for desktop input; inserted
knots interpolate original times and same-position pauses are retained. Its default
web canonicalization is unchanged. Too-long input
is retained untapered with a notice instead of silently discarding the gesture.
All resulting styles/samples live in ordinary editable native stroke events.

Validation uses synthetic gestures, not a crude redraw offered as finished art:
4px blue/0.6 opacity/hard-edge/taper event and visible pixels; mid-gesture switch to
40px red must not alter the already-started stroke; invalid config leaves settings
unchanged; the next gesture uses 40px red with assistance off; actual QML
width-slider moved signal updates the canvas controller. A simulated Qt tablet
pressure0.4→0.7 and100→120ms duration remain unchanged even with mouse assistance
enabled. These are synthetic events, not hardware pen acceptance. Native styled
strokes are saved/reopened with source-content equality checked.

Remaining goal gap: live stroke preview, richer brush texture/pressure response,
stabilization preserving intentional contour shape, source-faithful contour repair,
intentional layer/mask authoring and full composition/finish quality. No claim of
human-artist equivalence, automatic reconstruction quality, real tablet hardware or
Windows/D3D11 verification is made. Existing source layers and old work are retained.
