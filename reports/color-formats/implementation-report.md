# Color / Formats / Models Implementation Status

Date: 2026-10-08  
Branch: `impl/color-formats-models-c01-c07-m01-m06`

This report separates code/test evidence from environment-dependent acceptance gates.
A red or green status from one environment must not be substituted for another.

## Current status

| Area | Status | Evidence |
| --- | --- | --- |
| Portable Debug suite | Pass locally | Re-run after current PSD hardening through `5c924409`: 34/34 CTest entries completed with 0 failures; F05.NativeTransport and F06 are designed skips |
| C04/C05 Release | Pass locally | targeted C04/C05 2/2 |
| C04/C05 ASan | Pass locally | targeted C04/C05 2/2 |
| GitHub-hosted CI | Blocked | latest Ubuntu and Windows jobs fail before any workflow step executes |
| Independent PSD decoder cross-check | Supporting pass | ImageMagick 7.1.2-1 reads RGB8/16A and CMYK8/16A structures |
| Adobe Photoshop reopen oracle | Blocked | Photoshop is not available in the current execution environment |

## GitHub Actions blocker

The branch checks are currently red. This is not described as green or ignored.

For the latest workflow runs `37709589151` and `37709589043` at commit `5c924409`, both
Windows and Ubuntu jobs completed in roughly three seconds with `steps=[]`: checkout,
configure, build, and CTest never started. Earlier failed runs were re-run and produced the
same no-step failure; previous job inspection also reported no assigned runner and no
downloadable job log.

Therefore current GitHub status is **Blocked before test execution**, not a successful CI run
and not evidence of a C++ test failure.

## C04/C05 interoperability findings closed in this work

The following bugs were exposed by tests/spec-oracle review and fixed:

1. A valid flattened PSD with a zero-length layer/mask section returned before decoding the
   merged image.
2. Merged PSD/PSB image data supported only raw compression; RLE, ZIP and ZIP+Prediction
   were added, including PSB four-byte RLE scanline counts.
3. Layer visibility was not preserved through PSD writer/reader handling.
4. Merged transparency was treated as alpha merely from channel count. Photoshop uses the
   sign of the layer count to mark the first extra merged channel as composite transparency.
   Writer and reader now preserve/honor this marker.
5. RGB merged transparency was written as straight RGB even though Photoshop merged previews
   are stored matted against white. Writer now emits white-matted RGB preview samples and
   reader removes that matte; layer channel pixels remain unassociated.
6. CMYK file polarity and 8/16-bit paths remain separately covered.
7. Non-zero reserved PSD header bytes and duplicate layer channel IDs are rejected as corrupt
   instead of being accepted into later decode stages.
8. Unsupported layer channels (including real user mask `-3` and extra color/spot channels)
   now fail explicitly instead of being decompressed and silently dropped.
9. Legacy Photoshop advanced-layer tags `tySh` and `plLd` are rejected alongside their newer
   `TySh` / `PlLd` counterparts so text/placed layers are not misreported as ordinary pixels.
10. Layer clipping and non-zero user-mask flags are explicitly rejected until their rendering
    semantics are implemented; neither is silently flattened into editable raster semantics.
11. Flattened RGB(A) PSD output with zero layer records now emits Photoshop's merged
    transparency tagged block (`Mtrn` for 8-bit, `Mt16` for 16-bit), where negative zero layer
    count cannot encode transparency.
12. The streamed writer path includes the same flattened-transparency tag in its exact byte
    accounting, and the local full portable suite passes after this change set.
13. 16-bit Photoshop user masks now use an explicit full-range conversion to the internal
    8-bit mask representation: `(sample16 + 128) / 257`. Writer expansion remains `v*257`,
    so Drawborad-originated masks round-trip exactly at internal precision. Arbitrary external
    16-bit mask low bits are intentionally quantized rather than silently truncated.

The independent decoder check is intentionally not used as a substitute for Photoshop.
See `tests/oracles/photoshop/README.md` for the remaining acceptance procedure.

## Remaining gates

C04/C05 are not eligible for final product Pass until actual Photoshop reopen/save/reopen
evidence is recorded. In addition, unsupported Photoshop structures must continue to fail
explicitly rather than being silently reclassified as editable raster content.

Next review focus:

- unknown additional-layer-info handling at document scope beyond the blocked linked/smart
  object families;
- missing mandatory layer color-channel descriptors and other structurally incomplete records;
- Photoshop-oracle confirmation that the documented 16-bit-to-8-bit user-mask quantization
  policy is acceptable for product requirements; exact arbitrary 16-bit mask preservation is
  not provided by the current 8-bit internal mask model;
- Photoshop-generated fixture/oracle coverage for groups, masks, clipping rejection,
  blend modes and CMYK.

## Evidence discipline

The implementation plan explicitly requires independent PSD/Photoshop validation. Until the
Photoshop gate is run, status must remain **Blocked** for that acceptance dimension even when
portable local tests pass.
