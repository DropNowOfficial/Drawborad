# E04 - Critical lettering and logo review, consent-gated text candidates

Date: 2026-10-08
Base branch: impl/reconstruction-e03 (2a169253793968ad87e532409198e549b1131fb1)

## Implemented contracts

- `checkCriticalRegions`: independently evaluates user-specified lettering/logo ROIs by rendering stored stroke events with R01 and comparing against the read-only, native-profile source. Color-channel MAE, alpha MAE, and alpha-weighted edge mismatch are separate gates. The overall decision is the conjunction of every ROI; a large easy background cannot cancel a failed tiny logo.
- ROI sampling uses <=512-pixel renderer tiles with one-pixel neighbor overlap. Invalid bounds, profiles, stride, stale revisions, missing layers, cancelled work, and oversized inputs fail explicitly.
- The report never calls a brush layer editable Photoshop text. Font substitution is not an E04 output.
- `suggestText`: default path does no recognition; explicit consent and an installed local provider are both prerequisites. Candidate text is advisory and cannot mutate the source, change fonts, or create type layers. Missing offline provider is UnsupportedFeature, not fake OCR.

## Fresh verification and limitations

- TDD: 13 behavioral cases registered under E04. The stub was built and tested first: 10 assertion failures, 3 negative tests passing.
- GREEN: E04 13/13 test cases, 0 failures after fixing one invalid CMYK16 test datum.
- Historical source archive baseline full portable CTest: 35 entries; 1 known pre-existing M01 malformed-number failure, 2 designed skips, E04 passed.
- Applying previously merged upstream M01 JSON grammar fix `985efc4a` **only to the local verification copy**: 35/35 CTest entries completed, 0 failures, 2 designed skips. The validation overlay was reverted after testing.

This is a qualified Linux reference-engine test against archived source contracts, not a successful test of the exact GitHub E04/E03 combined head, Windows MSVC, Photoshop, or a real multilingual offline OCR model. Native channel MAE and simplified edge comparison are not Delta E00 or SSIM. E04 is not an OCR/font generation feature and never claims type-layer editability.

## Rulings and next gates

- Ruling: a checked `Result<CriticalRegionReport>` replaces an infallible `QualityReport`; corrupt inputs and stale renders cannot appear accepted. Downstream quality aggregation must unwrap the Result.
- Ruling: no real OCR provider is bundled in E04; the opt-in provider boundary is implemented, but fully offline multilingual OCR quality remains Blocked until verified provider packages and target-machine testing.
- E05: versioned Fidelity and four distinct manual-media style variants; do not mutate existing object/review identities.
- E06: three independent final quality gates remain Blocked until actual source images, Photoshop roundtrip, hardware, and human process judgments exist.
