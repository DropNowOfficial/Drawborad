# Native photo quality slice

Baseline: PR10 dc8a930078ed690d95936665695145af2172105e; all 755 blobs verified locally.

1. Reproduce original-size Chelsea (451x300) and coffee (600x400), CC0 scikit-image inputs. Start with explicitly diagnostic whole-image masks to isolate painting from segmentation. This is not semantic acceptance.
2. Reproduce narrow diagonal detail loss with an executable native render test. Compare the production sweep, not an old cached PNG.
3. Add bounded native multiscale corrections using original coordinates, local image directions and native coverage/blending feedback. Preserve ordinary StrokeEvent identities and storage. Reject unsupported layouts; no implicit resizing.
4. Compare actual renders and named source-size ROIs. Keep regressions visible; numerical improvement is not artistic acceptance. Exercise source-free save/reopen, editing and export.
5. Integrate only a verified candidate; retain the baseline route for comparisons. Run native/API/UI checks appropriate to the actual integration and record all failures. Publish a dedicated draft, never rewrite PR10.

Real SAM3, 3300x5100 automatic painting, Windows and paid cloud work are separate unverified gates. Do not label diagnostic masks as model output.
