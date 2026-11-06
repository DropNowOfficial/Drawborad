# Native browser stroke editor execution ledger

Baseline: fe03a2fd145eba6bb9d3ac48284e49df17bb8f48. PRs #4/#5 open, unmerged.
Architecture: existing HTML/FastAPI -> serialized private JSONL process -> ApplicationController/Document/ProjectStore. CLI-per-request loses undo; WASM adds an unnecessary toolchain. No second authoritative state model.

1. Native RecolorStroke, stable identity and atomic rejection. RED observed: no handler returned no dirty region; GREEN local C++ core suite 15 cases. Further native render/store coverage follows session test.
2. Persistent session open/create/state/command/undo/redo/save/close/preview/export with decimal-string revisions, explicit saved revision and restart semantics.
3. Thin single-worker API; timeout terminates uncertain native session, next access recovers saved disk state; no implicit unsaved recovery.
4. Existing page native editor with explicit stroke selection, native visibility/lock, replay, downloads and refresh reconnect.
5. Connect existing guided input/reconstruction and verify real-photo full edit loop, independent HTTP and browser.

Acceptance pending until actual execution; no remote public deployment is authorized. Current local host lacks CMake/Qt development libraries. CI performs complete native build. Dedicated branch only, no main merge.

## Final implementation checkpoint

- Native RecolorStroke and persistent session implemented; session contract passes.
- Project API implemented; native response is the only state projection. Auth, busy, stale, lock and unavailable errors remain distinct.
- Existing page editor and human-guided photo entry implemented. Syntax checked; browser interaction and visual QA blocked by explicit cloud-browser URL policy.
- Actual Chelsea photograph independent HTTP acceptance completed: 4,391 strokes; only selected stroke recolored; save/reopen/replay after removing references; independent ORA max error 0; server restart restores saved revision. This is backend acceptance, not browser acceptance.
- Independent review completed once; bounded fixes made for failed replacement/history, oversized IPC and rejected UI properties. Server regression tests pass.
- PR #6 is draft. No merge or public deployment. Final CI is attached to the implementation commit; inspect the run, not historical green status.
- Resume with authorized browser access and actual end-to-end UI verification.
