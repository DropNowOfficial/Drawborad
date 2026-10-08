# Photoshop Oracle

Status: **Blocked for actual Adobe Photoshop reopen validation in the current environment.**

This directory is the acceptance anchor for C02/C04/C05 Photoshop interoperability.
An internal Drawborad writer -> Drawborad reader round trip is useful regression coverage,
but it is not sufficient evidence for Photoshop compatibility.

## Independent decoder cross-check

A local cross-check was run against current C04/C05 output with ImageMagick 7.1.2-1
(Q16-HDRI) after the merged-transparency fixes.

Observed:

- RGB8 + alpha is recognized as PSD RGBA.
- RGB16 + alpha is recognized as PSD RGBA.
- CMYK8 + alpha is recognized as PSD CMYKA.
- CMYK16 + alpha is recognized as PSD CMYKA.
- Independent parsing exposes both layer and merged-image subimages.
- CMYK channel polarity remains stable in the independent decoder.
- RGB merged transparency now follows Photoshop's white-matted preview convention;
  independent decoding no longer produces the previous dark/black-fringe failure.

This is supporting evidence only. It does **not** replace Adobe Photoshop acceptance.

## Required Photoshop C05 acceptance

Until these checks are executed in Adobe Photoshop, C05 remains Blocked rather than Pass.

For each RGB8, RGB16, CMYK8, and CMYK16 fixture:

1. Open the exported PSD in Photoshop.
2. Record Photoshop version, OS, fixture SHA-256, and Drawborad build/commit.
3. Verify document dimensions, bit depth, color mode, ICC profile, and PPI.
4. Verify layer count, layer order, Unicode names, groups, visibility, and opacity.
5. Verify all enabled blend modes: Normal, Multiply, Screen, Darken, Lighten.
6. Verify user-mask bounds, default value, offsets, and visible result.
7. Verify merged transparency/appearance independently from layer pixels.
8. For CMYK, verify native C/M/Y/K values and K-channel polarity.
9. Save As a new PSD, close it, reopen it, and repeat the structural checks.
10. Compare representative pixels/channels against the source fixture and record evidence.

A thumbnail looking correct is not sufficient. The layer structure and native color channels
must survive the round trip.

## Current CI note

GitHub-hosted CI is presently blocked before runner execution on this branch: the affected
jobs complete with no executed steps. Local CTest results therefore remain separate evidence
and must not be described as GitHub CI green.


## Frozen implementation scope

C04/C05 implementation review is frozen after the bounded final pass. The codec now either
preserves the supported raster/group/mask semantics covered by tests or rejects known
unsupported visual structures explicitly. Non-default layer blending ranges (Photoshop
Blend If), non-zero user-mask flags, global layer masks, linked/smart-object families, and
other blocked high-impact visual structures are not silently reclassified as ordinary
editable raster content.

Do not expand PSD tag coverage merely because another Photoshop tag exists. Reopen this
implementation round only when the Photoshop reopen/save/reopen oracle or a concrete
real-world fixture demonstrates a Critical/Important corruption, visual mismatch, or silent
data-loss bug.
