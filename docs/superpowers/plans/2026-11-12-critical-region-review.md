# Critical-region review slice after PR #8

Baseline f9085f9934b5f0c2dba42b0433ecde15c862a794 (PR #7), full CI 43106526949 success. Read PR #8 log at 97b150bb5ae069b4f1da9594d7e776ffe7031e7a: documentation only, no repair code. Do not reconstruct unpublished v3/v4 changes from prose.

Scope: inspect source-derived artwork at identical original-pixel dimensions; full-frame and explicit critical-region diagnostics; preserve private input provenance; never infer artistic/native/print acceptance from pixel numbers. No native painting state or format is changed.

Plan: (1) write failing measurement/safety tests, (2) implement bounded self-contained review, (3) run on an existing actual C++ render and inspect, (4) fresh review, (5) publish isolated draft and CI.

RED: 11 behavior cases failed against NotImplemented stubs. GREEN: all 11 pass. Covers localized defects hidden by global averages, blur edge-energy loss, transparent RGB, invalid regions, dimension mismatch, ICC mismatch, HTML escaping, no overwrite and no automatic artistic acceptance.

Ruling: cap this first tool at the native baseline's 4 Mi-pixel budget; reject larger files explicitly, never resize. It cannot yet qualify a 3300×5100 poster. This avoids pretending print-size support exists.
Ruling: use encoded-pixel diagnostics, explicitly not SSIM/DeltaE or perceptual ICC validation. Do not substitute them for existing QualityEvaluator or human process review.
Ruling: leave unpublished repairs and PR #8 documentation branch untouched. This adds an independent review tool, not the missing v3/v4 implementation.

Final fresh review: unsupported 16-bit RGB PNG could silently become RGB8 and hide errors; regression failed, then encoded-depth validation rejects it before decode. A template substitution could alter a valid REPORT region label (and potentially embedded evidence); regraded Important for artifact integrity, RED→GREEN, one-pass substitution fixed. Input hashes now use exactly the decoded byte buffers.
Review checked alpha arithmetic, ICC labeling, bounded inputs, no overwrite, and explicit unverified gates; no additional actionable findings.
