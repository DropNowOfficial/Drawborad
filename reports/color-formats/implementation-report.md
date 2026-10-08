# Color / Formats / Models Implementation Status

Date: 2026-10-08  
Branch: `impl/color-formats-models-c01-c07-m01-m06`

This report separates code/test evidence from environment-dependent acceptance gates.
A red or green status from one environment must not be substituted for another.

## Current status

| Area | Status | Evidence |
| --- | --- | --- |
| Portable Debug suite | Pass locally | 34/34 CTest entries passed; F05.NativeTransport and F06 are designed skips; 0 failures |
| C04/C05 Release | Pass locally | targeted C04/C05 2/2 |
| C04/C05 ASan | Pass locally | targeted C04/C05 2/2 |
| GitHub-hosted CI | Blocked | latest Ubuntu and Windows jobs fail before any workflow step executes |
| Independent PSD decoder cross-check | Supporting pass | ImageMagick 7.1.2-1 reads RGB8/16A and CMYK8/16A structures |
| Adobe Photoshop reopen oracle | Blocked | Photoshop is not available in the current execution environment |

## GitHub Actions blocker

The branch checks are currently red. This is not described as green or ignored.

For workflow runs `37702484908` and `37702484862`, the failed jobs complete within
seconds without executing checkout, configure, build, or CTest. Re-running the failed jobs
produced the same no-step failure. Previous job inspection also reported no assigned runner
and no downloadable job log.

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

The independent decoder check is intentionally not used as a substitute for Photoshop.
See `tests/oracles/photoshop/README.md` for the remaining acceptance procedure.

## Remaining gates

C04/C05 are not eligible for final product Pass until actual Photoshop reopen/save/reopen
evidence is recorded. In addition, unsupported Photoshop structures must continue to fail
explicitly rather than being silently reclassified as editable raster content.

Next review focus:

- linked/smart-object document tags such as `lnkE` and related linked-layer structures;
- unknown additional-layer-info handling at document scope;
- hostile/duplicate/missing layer channel descriptors;
- 16-bit user-mask preservation semantics;
- Photoshop-generated fixture/oracle coverage for groups, masks, blend modes and CMYK.

## Evidence discipline

The implementation plan explicitly requires independent PSD/Photoshop validation. Until the
Photoshop gate is run, status must remain **Blocked** for that acceptance dimension even when
portable local tests pass.
