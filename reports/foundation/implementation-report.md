# F01–F06 implementation and verification report

Scope: approved native foundation plan only. Original remote plan commit:
`42bedbf24854d6c8bd1e1a4a85eb37bf5eb4141e`.

## Honest status

| Task | Implemented | Evidence | Remaining acceptance |
|---|---|---|---|
| F01 | Dependency verifier, explicit developer acquisition, offline verification, presets, task runner | 12 verifier + 4 runner tests; deliberately unqualified Windows lock returns exit 2 | Real Windows artifact acquisition, digests, DLL/ABI/security/license qualification **Blocked** |
| F02 | Checked free dimensions, PPI, resource estimates, RGB/CMYK U8/U16 descriptors | 6 native cases, including overflow/NaN/custom aspect/CMYKA16 | Target MSVC build not run |
| F03 | Object/stage tree, immutable snapshots, resource DAG, commands, undo/redo | 14 native cases including 10,000 edits/undo/redo, locked ancestors and mutable-alias rejection | Pixel flattening is not implemented here; composite merge/split preserves structure |
| F04 | Tile store, SHA-256, delta journal, atomic save/backup, recovery, leases, autosave clock, history budget | 14 native cases; injected write/flush/replace failures; every-byte truncation; real file roundtrips | Windows flush/replace/locking and power-loss qualification **Blocked** |
| F05 | Worker protocol, stale/nonce/hash checks, resource admission, cancellation; Win32 transport source | 12 native policy cases plus real child-process probe | Win32 pipe/Job Object/read-only mapping test **Blocked** |
| F06 | Controller, Qt/QML shell source, diagnostic CLI and platform smoke runner | 4 controller/diagnostic cases and real Linux diagnostic executable | Qt/MSVC build, WARP and actual hardware D3D11 startup **Blocked** |

**The six tasks are implemented at source level with tested portable components;
full Windows acceptance is not complete.** No Windows EXE/installer is presented.
F01 in particular is intentionally not marked complete: its vendor qualification
artifacts cannot be invented. Later rendering/ICC/PSD/AI/UX tasks are not claimed.

## Actual runs

Each final configuration (`lab-debug`, `lab-release`, `lab-asan`) ran ten CTest
entries: **eight passed, zero failed, two skipped**. The skipped entries are
`F05.NativeTransport` and `F06`, both guarded by an explicit Windows prerequisite.
CTest's generic “100% tests passed” headline includes these skips; this report does
not count them as passes. See the three JUnit files and `test-summary.json`.

The ASan run enabled `detect_leaks=1:halt_on_error=1`; UBSan used `halt_on_error=1`.
The portable suite contains 50 C++ behavior cases and 16 Python unit cases. A real
native worker process additionally handles hello/cancel and rejects empty,
truncated and oversized frames. The policy fake worker tests do not qualify the
real Windows transport.

No benchmark was run on the user's 185H/32GB/4070 Laptop machine. Linux timing is
not quoted as Windows performance. No real Qt render adapter or CUDA device was
invented in diagnostics; absent fields remain absent.

## Review and regressions

Review was performed by the author in a separate pass; no independent reviewer or
subagent tool was available. This is weaker than an external review before merge.

Observed-failing regression tests were added before fixing:

- Imported snapshot aliases could mutate document nodes/resources/events.
- A serialized descriptor could bypass overflow/estimated-size checks.
- Splitting a modified composite silently discarded opacity/transform state.
- Group transforms could bypass a locked descendant.
- Saving after backup recovery could copy the corrupt primary into the only good backup.
- Conflicting content with an identical revision could overwrite a project.
- A revision-zero document was skipped by the autosave timer.
- Worker output could rewrite existing event history or append to a locked layer.
- Authenticated malformed terminal output retained its heavy-resource reservation.
- Color-format changes did not recompute byte estimates.
- Undo omitted removed node IDs, and event-only worker commits omitted invalidation.
- The test runner could count a skipped task as Green or a crash as behavior Red.

The final whole suite passed after these fixes. The Windows-only source review also
changed asynchronous I/O cleanup: OVERLAPPED data is retained until completion on
an owned cleanup thread; cancellation requested before pipe connection is queued.
This change is **not** called runtime-verified: its actual platform test is blocked.

## Decisions and costs

1. **Isolated local snapshot, connector publication.** Git DNS/transport was not
   available. Implementation used a linked worktree and local task commits, with
   connector publication preserving the remote plan tree and `main`. Local and
   remote commit identities differ; publication must compare per-file Git hashes.
2. **Portable lab versus Windows qualification.** A dependency-free C++20 runner
   was used instead of unavailable Catch2/QtTest binaries. Cost: the native Windows
   build, SDK, Qt and complete vendor lock remain unqualified, not silently waived.
3. **Plan input.** The mounted archive has expanded wording relative to the remote
   plan. Remote F01–F06 interfaces/acceptance remain authoritative. Cost: deviations
   are recorded here rather than assumed equivalent.
4. **Execution evidence.** Skill helper scripts were not mounted and numeric
   headings did not match Fxx IDs. Equivalent briefs, RED/GREEN logs and a ledger
   were maintained. Cost: no independent reviewer or automated skill-runner record.
5. **Non-destructive merge.** Merge creates a composite/provenance group; it does
   not prematurely raster-flatten layers. Split refuses changed group visual
   properties rather than losing them. Cost: rendering/baking remains with R01/C02.
6. **Immutable ownership.** Commands share immutable resources/events; import
   detaches external aliases. Cost: large event appends still require the planned
   packed-index optimization; this milestone is not the million-stroke renderer.
7. **Delta journal.** Journal records contain changed metadata/resources/events,
   not repeated whole snapshots. The bounded sidecar is atomically rewritten at
   append. Cost: write amplification requires compaction and Windows qualification
   before large-scale use.
8. **Explicit storage bounds.** Records are capped at 512 MiB, individual resources
   at 64 MiB and worker snapshots at 64 MiB. Oversize data fails before unchecked
   allocation. Cost: later streaming work is needed for large self-contained files;
   these limits are not marketed as production 4K/64-layer export capacity.
9. **Worker scope.** `native.smoke` demonstrates isolation/cancellation only. It is
   not a pretend reconstruction model. Only verified staged deltas can commit;
   preexisting history is append-only. Cost: real task adapters are later modules.
10. **Conservative external invalidation.** Worker commits report exact changed
    IDs but may dirty the complete canvas until R01 defines brush footprints.
    Cost: larger redraw after a completed batch, not per-frame history replay.
11. **Color conversion contract.** F03 commits explicitly preconverted payloads
    and paper values; it does not simulate an ICC engine. Cost: C01 must supply them.
12. **Native-only fixes.** Windows-specific cleanup and QML guards were reviewed
    but not executed here. Cost: platform defects may remain and block release.

Deferred minor: DXGI's raw diagnostic driver fields are not yet presented in a
polished consumer-facing label. Native diagnostics preserve measured information.
No release blocker is relabeled as a minor.

## Next mandatory acceptance, not a new product questionnaire

Acquire/qualify the real Windows dependency set, compile with the pinned MSVC/Qt
combination, run `F05.NativeTransport` and `F06` on WARP and the actual GPU, and
exercise native durability and window responsiveness. Do not change skipped tests
to unconditional passes or mark the release lock qualified without evidence.

Relevant implementation references (interfaces, not proof of our runtime behavior):

- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
- https://learn.microsoft.com/en-us/cpp/sanitizers/asan
- https://doc.qt.io/qt-6/qquickwindow.html
