# RC1 execution ledger

Plan: `docs/superpowers/plans/2026-10-26-integrated-alpha-rc1.md`.

- Baseline: exact GitHub ref/source tree verified, 299 blobs, clean isolated local branch. GitHub plugin created the requested remote branch at the exact base; no Actions workflow was dispatched.
- Historical Windows build/cache/model directories are not mounted and no remote Windows executor is exposed. Native acceptance remains Blocked.
- Ruling: the user supplied a complete execution/acceptance contract; implement within it without an additional design approval cycle. No change to platform or acceptance thresholds.
- Ruling: run independent engineering changes in disjoint file scopes to avoid an unbounded serial audit. Root owns build/test evidence; model and UI agents agree a readiness receipt; packaging consumes root's inventory/report.
- Pre-flight: model readiness -> worker/UI: stable enum names, manifest/encoder/decoder hashes and real smoke receipt; no static-only Ready.
- Pre-flight: test profile/report -> packaging: default strict integration; explicit UI-only; full SHA/tree/config identity; missing tests cannot be green.
- Pre-flight: UI/protocol: preserve existing protocol/build-token checks; extend only allowed task routing.
- Baseline portable build uses a Git archive of the starting SHA, isolated from concurrent edits. Final candidate results will be recorded separately.
- Recovered the official 600x400 coffee original and verified the requested SHA-256. No source image, SDK or model weight is added to the repository.
- Provisioned isolated Linux CPU ORT and codec development dependencies from official sources with hashes/signature evidence. Existing matching host runtimes were reused; no system install or silent SAM download occurred.
- Whole-branch read-only review identified worker/test output-directory mismatch, ambiguous codec executable discovery and stale configuration receipt reuse. All were corrected; regression evidence remains separate from final committed-snapshot tests.
- A transient execution-environment interruption left some generated Linux executables empty or non-executable. Initial failures were retained; only those generated outputs were relinked or had executable permission restored, with byte hashes checked. The interruption cause remains unestablished.
- Official PowerShell AST parsing ran on Linux. This proves syntax only and is not native deployment execution.
- Candidate policy: full-source SHA, full effective CMake configuration, actual model/SDK/reference hashes, complete registration and one complete CTest execution must agree. Neither historical green runs nor selected rechecks replace a final complete-suite receipt.
- Native Windows and Photoshop remain unavailable. Continue with reproducible scripts and honest Blocked gates, without producing a source archive labelled as an application.
