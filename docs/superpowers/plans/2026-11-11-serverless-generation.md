# Mission 3 execution ledger
Baseline: 8c30457bd5c7bee7a6847b0b05b5b1f7bf621aec. PR6 open/draft; three latest runs successful. New branch work/serverless-generation-20261111. Existing branches remain untouched.

Goal: two independent finite jobs, persistent reviewed masks between them, true native artifacts afterwards. Native snapshots remain the only painting state. No always-on GPU or mandatory CPU VPS.

1. Contract: explicit fixture, conditional durable job state, selected-object native CLI, once-only workers; native output/integrity tests.
2. Provider adapters: private S3/R2 files and conditional state; official SAM3 image GPU handler; bounded RunPod dispatch and unknown-submission handling. Real deployment requires account/model/storage access.
3. Review API/UI: upload, status, mask acceptance, build, download; retain editor separately. No implicit acceptance.
4. Integration: independent HTTP and browser if reachable; source removal/reopen/export, failure and duplicate checks.
5. Deliver dedicated Draft PR, CI and evidence, actual cost status. ARES_207 remains untested until authentic input accessible and actual model run.

Ruling: use native --accept-index and explicit --fixture loader opt-in. Existing CLI only exposed --accept-all despite core supporting selected indices. Do not relabel fixture as SAM.
Ruling: local persistent-directory backend proves restart contract only; S3/R2 adapter needs real cloud acceptance. No cost or scale-to-zero claims from mocks.
Pre-flight: GPU output uses existing complete disjoint manifest; review stores selected stable candidate IDs; CPU translates them to immutable manifest indices. Names include view identity; no cross-view merge.
RED: native selective fixture test failed with Invalid arguments against baseline binary. Initial Ubuntu binary could not load libjpeg.so.8 on Debian; replaced with exact-baseline Debian CI artifact before measuring red.
External status: plugin discovery found no RunPod/Cloudflare administration connection. No actual paid jobs submitted. SAM license/access and storage credentials not verified.

Task 1 local: 11/11 batch tests pass using Debian native artifact from PR test merge dbc55eb (source 59755c3); selective fixture produces genuine events and source-free replay. Local fixtures use filesystem persistence, cloud adapter remains unqualified. Ubuntu CI failed before compilation because archive/security.ubuntu.com DNS failed; Debian compiled and passed native tests.
Ruling: first paid trial uses a persistent single reservation; uncertain provider responses require reconciliation and never automatic resubmission. This sacrifices automatic retry to avoid duplicate paid jobs.

## Continuation verification 2026-11-11
- Current PR #7 head before this continuation: 59755c37168c43a2940ec2de32843b5974074f2d; base unchanged 8c30457.
- Regression: 33 passed locally (cloud + batch), actual new native binary. Missing .devcontainer/script in local snapshot restored unchanged from baseline before rerun.
- Three new failure tests observed RED then GREEN: invalid image/review payload, crashed-worker expiry on read, failed SAM attempt billing left unreconciled.
- Independent HTTP on existing 192x128 Chelsea test input: fixture partition, explicit one-object acceptance, 2284 native strokes / 4 paint layers. Gateway stopped/restarted before review and again after generation; downloads survived. Native input removed before reopen; independent ORA composition passed. No resizing performed by this run.
- Initial CI 43059646454: Debian build/test success; Ubuntu failed package-source DNS before compilation; dependent browser/runtime jobs skipped. New Chromium batch acceptance added to both native build matrix entries.
- Ruling: keep production CPU dispatch disabled until a real finite CPU provider is connected; never silently charge GPU for reconstruction. Cost: production build is unavailable until configuration.
- Actual GPU/S3/SAM3/ARES_207 and platform billing remain unrun; no paid resources created. Docker deployment recipes are unbuilt.

## Final fresh-context review
- Important fixed: provider cancellation timeout previously made retries impossible. RED→GREEN regression now retains terminal publication barrier and persists pending/requested cancellation; retry is allowed. A provider request acknowledgement does not prove billing stopped.
- Important fixed: numeric/bool/null view IDs accepted before model calls; RED→GREEN validation rejects all before dispatch.
- Minor deferred: changing prompt/view/backend without reselecting the image retains its idempotency key and returns a visible 409. Workaround: reselect image.
- Review boundary ruling: actual provider semantics, model/image compatibility, S3 privacy/CAS, costs/accuracy/ARES_207 and CPU deployment stay explicitly unverified until credentialed execution; no success claim follows from code review. Cost if wrong: deployment remains unavailable until qualification.
