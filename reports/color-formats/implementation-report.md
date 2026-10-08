# Color / Formats / Models Implementation Status

Date: 2026-10-08  
Branch: `impl/color-formats-models-c01-c07-m01-m06`

This report separates code/test evidence from environment-dependent acceptance gates.
A red or green status from one environment must not be substituted for another.

## Current status

| Area | Status | Evidence |
| --- | --- | --- |
| Portable Debug suite | Historical pass locally | Exact branch state through `7ca31e57`: 34/34 CTest entries completed with 0 failures; F05.NativeTransport and F06 are designed skips |
| C04/C05 final-round targeted tests | Pass in archived-source repro | Final bounded patches: C04 17/17 cases and C05 20/20 cases, 0 failures |
| Archived-source full regression | Qualified partial evidence | 34 CTest entries: C04/C05 pass; one old-snapshot M01 failure; F05.NativeTransport and F06 are designed skips |
| GitHub-hosted CI | Blocked | latest Ubuntu and Windows jobs fail before any workflow step executes |
| Independent PSD decoder cross-check | Supporting pass | ImageMagick 7.1.2-1 reads RGB8/16A and CMYK8/16A structures |
| Adobe Photoshop reopen oracle | Blocked | Photoshop is not available in the current execution environment |

## GitHub Actions blocker

The branch checks are currently red. This is not described as green or ignored.

For the latest code checks at commit `fc0b7e1a`, both Windows and Ubuntu jobs again completed in roughly three seconds with `steps=[]`: checkout, configure, build, and CTest never started. Earlier failed runs were re-run and produced the same no-step failure; previous job inspection also reported no assigned runner and no downloadable job log.

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
14. Section-divider blend metadata (`lsct`/`lsdk`) now overrides the layer-record blend for
    the five supported modes; unsupported pass-through groups fail explicitly instead of
    silently behaving as Normal.
15. Truncated trailing additional-layer-info headers are rejected instead of being silently
    treated as absent metadata.
16. The decoded-byte budget is enforced cumulatively across retained layer pixels, masks and
    merged composite data rather than independently per allocation.
17. Image Resource 1053 (Alpha Identifiers) is parsed and used to distinguish saved alpha
    channels/selections from actual merged transparency. Identifier counts are checked against
    the number of extra document channels.
18. Transparency evidence now follows Photoshop semantics: merged-transparency document tags
    and negative layer counts take precedence; a positive layer count remains authoritative
    unless an explicit merged-transparency tag is present. Saved alpha metadata therefore no
    longer turns a selection channel into document alpha.
19. Non-empty global layer mask data and known document-scope visual mask/effect blocks
    (`LMsk`, `FMsk`, `FXid`, `FEid`) now fail explicitly as `UnsupportedFeature` rather than
    being skipped while the document is reported as faithfully editable.
20. User-mask default values are validated against the PSD-defined 0/255 domain on both read
    and write paths. Unsupported non-zero mask flags remain explicit `UnsupportedFeature`.
21. PSD writer preflight now requires balanced Photoshop group section dividers in file order;
    unmatched folder records or bounding dividers are rejected instead of emitting malformed
    group structure.
22. Layer blending ranges / Photoshop Blend If are no longer silently discarded. Canonical
    full-range defaults are accepted, while non-default ranges that would alter compositing
    fail explicitly as `UnsupportedFeature`. Malformed range lengths fail as corrupt data.

The independent decoder check is intentionally not used as a substitute for Photoshop.
See `tests/oracles/photoshop/README.md` for the remaining acceptance procedure.

## Bounded final review and freeze

The final C04/C05 review was deliberately limited to three high-impact areas: document-scope
metadata that can change visual semantics, standard user-mask semantics, and writer-emitted
group/mask structure. Only Critical/Important correctness issues entered the final fix pass.

The one Important self-review finding was silent acceptance of non-default layer blending
ranges (Photoshop Blend If). It was reproduced RED, changed to explicit unsupported while
retaining canonical no-op defaults, and verified GREEN with the targeted C04/C05 tests.
No further Critical/Important finding remained within the deliberately bounded C04/C05 review
scope after that fix pass. This was a self-review because no independent reviewer subagent was
available in the current environment.

Fresh reproduction used the archived successful CI source snapshot at commit `65b9c0be` with
only the final-round PSD patches applied. Targeted C04/C05 tests passed (17/17 and 20/20).
The snapshot's full 34-entry CTest run has one known M01 malformed-number failure; that exact
M01 defect is fixed later on this branch by commit `985efc4a`, so the archived full-suite run
is not represented as an exact-current-HEAD result.

**C04/C05 are now frozen for this implementation round.** Do not resume opportunistic PSD tag
coverage. Reopen only for (a) a Photoshop oracle result, or (b) a concrete real-world fixture
showing a Critical/Important corruption, visual-semantic error, or silent data loss.

## Remaining gates

C04/C05 are not eligible for final product Pass until actual Photoshop reopen/save/reopen
evidence is recorded. GitHub-hosted CI is independently blocked before workflow execution.
Neither blocker is a reason to expand PSD feature scope.

Deferred by explicit ruling for this round:

- non-zero user-mask flags (relative position, disabled/invert/rendered/parameterized masks)
  remain `UnsupportedFeature`; they are not silently reinterpreted;
- unknown or unsupported advanced Photoshop structures are not promoted to editable raster
  semantics; known high-impact visual families are blocked explicitly;
- exact arbitrary 16-bit user-mask preservation is outside the current 8-bit internal mask
  model; import uses the documented full-range quantization policy;
- Photoshop-generated reopen/save/reopen evidence remains the external acceptance gate.

## Evidence discipline

The implementation plan explicitly requires independent PSD/Photoshop validation. Until the
Photoshop gate is run, status must remain **Blocked** for that acceptance dimension even when
portable local tests pass.
