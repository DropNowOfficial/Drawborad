# Release hidden-boundary build repair

PR #10 head `36d053a` failed both Debian 13/GCC 14 and Ubuntu 24.04/GCC 13
Build jobs in run 43333522531. The subsequent native/browser checks were skipped.
The earlier local 85/85 result was a Debug build, not evidence of Release success.

The failing inlined function was `hiddenBoundary::resource`: repeated vector
insertions from the magic bytes to the source ID/hash and flag caused GCC's
`-O3 -Werror=stringop-overflow` diagnostic. Reproduced locally with GCC 14.2 using
the same optimization/warning flags. This did not establish an actual overflow:
the writes were standard vector insert/push operations.

The serializer now sizes its fixed 57-byte prefix before copying the magic,
source ID and hash and assigning the enable flag. The remaining field encoding
is unchanged. No warning suppression, format change, budget relaxation or
production validation removal.

Verification before publication:
- Original translation unit fails at `-O3`; patched final unit compiles with the
  same strict flags.
- Original and patched serialized bytes are identical for 2 and 256 points,
  with the enable flag both false and true.
- Added regression assertions for the fixed prefix, payload length, points,
  source dependency and content hash at those limits.
- B19.HiddenBoundary and B19.BoundarySession pass locally.

Remote Release CI must be checked on the resulting new head. These checks do
not certify visual artwork quality or Windows desktop acceptance.
