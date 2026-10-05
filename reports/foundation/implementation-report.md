# F01–F06 implementation and verification report

Scope: approved native foundation plan only. Original remote plan commit:
`42bedbf24854d6c8bd1e1a4a85eb37bf5eb4141e`.

## Honest status

| Task | Implemented | Evidence | Remaining acceptance |
|---|---|---|---|
| F01 | Dependency verifier, explicit developer acquisition, offline verification, presets, task runner | Windows 11 x64 validation configure/build succeeds with MSVC 19.42 + Qt 6.12; F01 and F01.Runner pass | Release dependency artifact digests, ABI/security/license qualification are still pending |
| F02 | Checked free dimensions, PPI, resource estimates, RGB/CMYK U8/U16 descriptors | MSVC Windows test passes, plus portable overflow/NaN/custom-aspect/CMYKA16 coverage | No foundation blocker |
| F03 | Object/stage tree, immutable snapshots, resource DAG, commands, undo/redo | MSVC Windows test passes; portable suite includes 10,000 edits/undo/redo, locked ancestors and mutable-alias rejection | Pixel flattening remains a later rendering task |
| F04 | Tile store, SHA-256, delta journal, atomic save/backup, recovery, leases, autosave clock, history budget | MSVC Windows test passes; injected failure and truncation recovery cases pass | Destructive power-loss qualification remains a later release durability test |
| F05 | Worker protocol, stale/nonce/hash checks, resource admission, cancellation; Win32 transport | F05, F05.NativeTransport and F05.ProcessProtocol all pass on Windows | No foundation blocker |
| F06 | Controller, Qt/QML shell, diagnostic CLI and platform smoke runner | Qt/MSVC build passes; WARP and hardware D3D11 startup pass; active adapter verified as RTX 4070 Laptop GPU | No foundation blocker |

**The Windows development-validation gate for F01–F06 now passes.** This does
not mean release dependency qualification, installer qualification, destructive
power-loss testing, later renderer/ICC/PSD/AI work, or production benchmarks are
complete.

## Actual runs

Portable final configurations (`lab-debug`, `lab-release`, `lab-asan`) ran ten
CTest entries: **eight passed, zero failed, two skipped** because Windows-only
transport and window tests were unavailable in the lab environment.

A subsequent Windows 11 x64 validation run on the target machine configured and
built the Qt desktop target with MSVC 19.42 and Qt 6.12. The complete validation
suite then reported **10/10 tests passed, zero failed**. This includes
`F05.NativeTransport`, `F05.ProcessProtocol`, and the `F06` WARP/hardware Qt
startup test.

The ASan run enabled `detect_leaks=1:halt_on_error=1`; UBSan used `halt_on_error=1`.
The portable suite contains 50 C++ behavior cases and 16 Python unit cases. A real
native worker process additionally handles hello/cancel and rejects empty,
truncated and oversized frames. The policy fake worker tests do not qualify the
real Windows transport.

The target-machine diagnostic run reports an Intel Core Ultra 9 185H, 32 GB RAM,
22 logical threads, Direct3D 11 scene graph, and an NVIDIA GeForce RTX 4070 Laptop
GPU with about 8 GB dedicated memory. Qt's active render LUID
`000000000001167b` matches the NVIDIA adapter LUID and the CUDA device LUID.
The Qt scene-graph log independently reports the NVIDIA adapter as “using this
adapter”. No WebView modules were detected. These are validation facts, not a
rendering-performance benchmark.

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

## Next engineering gate

The Windows development-validation blockers are closed. Proceed to the next
approved implementation batch: tile-backed canvas/render cache and color/file
interoperability work.

Release dependency qualification remains separate. Do not mark the release lock
qualified until the shipped dependency artifacts have real digests, ABI/security
checks, and license records. Destructive power-loss, installer, Photoshop
interoperability, and long-duration performance tests also remain later release
gates.

Relevant implementation references (interfaces, not proof of our runtime behavior):

- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
- https://learn.microsoft.com/en-us/cpp/sanitizers/asan
- https://doc.qt.io/qt-6/qquickwindow.html


## Windows target-machine validation — 2026-10-05

Observed target-machine results supplied from a fresh PowerShell session:

- Global `PATH` still resolved an unrelated P4V `Qt6Core.dll` first.
- CTest nevertheless passed F06 after the project added a test-local Qt 6.12
  `PATH` prepend. This confirms the runtime-isolation fix rather than relying on
  a modified global environment.
- `ctest --preset win-validation --output-on-failure`:
  **10/10 passed, 0 failed**, total reported time 4.49 s.
- Qt scene graph: `Direct3D11`.
- Qt D3D11 adapter: `NVIDIA GeForce RTX 4070 Laptop GPU`.
- Active render LUID: `000000000001167b`.
- NVIDIA adapter LUID: `000000000001167b`.
- CUDA device 0: `NVIDIA GeForce RTX 4070 Laptop GPU`, same LUID.
- Intel Arc adapters were enumerated but not selected.
- Microsoft Basic Render Driver was enumerated as software but not selected.
- Diagnostic warnings: none.

The reported OS version string is `Windows 10.0.26200`; Windows 11 uses the NT
10.0 version family, so this string is not treated as evidence that the machine is
running Windows 10.
