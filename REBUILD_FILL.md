# Current Drawborad reconstruction checkpoint

## Goal and acceptance

Build an editable layered drawing workflow and use it for a complete high-quality
work in the user's red DELETE cover / six-page comic direction. Tool correctness
is necessary but does not establish artistic quality. Preserve the chosen
composition, natural figures, varied clean marks and independent editable layers;
do not call low-resolution reconstruction newly recovered native detail.

## Opt-in under-ink edge coverage (local integration, CI pending)

The bucket now offers a default-off, bounded 2-pixel under-ink extension. The
original flood and its tolerance are unchanged. Only extension pixels are limited
to nonwhite, nontransparent pixels of one locked, unmasked, untransformed upper
black/gray original-lineart raster over a root-level color layer. Unsupported
stacks and colored sources reject rather than guess. Original PNG/raw resources
remain unchanged. Values 0/1/2 are supported by the native command; 0 is the old
behavior. Existing bucket engine-8 explicit runs persist the result without a new
file format or changes to old schema meanings.

Local strict C++ tests passed for gray and partially transparent edges, untouched
white regions, unchanged resources, default identity, undo/redo and rejected
stacks. A local source-derived hollow contour gained 606 ink-only pixels at 1px
and 753 at 2px; visible AA fringe reduced, but true-white gaps intentionally remain.
This is source-contour calibration, not new high-resolution detail or a finished
cover. All local JS tests and eight transport tests passed. Permanent synthetic C++
and Qt-session cases plus actual Chromium edge-control/undo/reopen checks are
registered. New Qt/browser integration has not yet been verified.

## Cropped ORA leaf export (local integration, CI pending)

Cropped MyPaint leaves now export their actual ROI dimensions and global x/y
placement, rather than a full transparent canvas for every stroke. Other layer
exports and .draw formats are unchanged. Local strict C++/libpng plus independent
Pillow verification exercised an 8x6 leaf at (510,509) in a 3300x5100 canvas,
Multiply at 50% opacity, global mask sampling across a 512 tile boundary, existing
1Mi-pixel ROI budget, and unchanged full-image recomposition. GIMP 3.0.4 console
import verified the candidate's cropped dimensions, offsets, blend and opacity;
this was not a desktop GUI test. The small leaf's pixel buffer is 192 bytes instead
of 67,320,000, not a claim that the whole app uses only that memory. Full merged
canvas and paper still exist. New integrated CI remains pending.

## CI and durable continuation

Latest published checkpoint before edge coverage: d23ba38, portrait contained-image
mouse coordinate mapping. Remote text audit read all 144 test/QA files; obsolete
raster/native-count assertions were corrected in 61eb204 and 640ac61. Existing
behavior assertions were retained. d23 cloud runtime passed; Linux/native/browser
run 43217346552 is still in progress. The last fully verified MyPaint GUI is
512x512 at e5a845e, not the new poster workflow.

Local actual native poster experiment: 3300x5100, six bounded cropped strokes,
undo/redo, save-copy/reopen pixel equality and independently recomposed ORA passed.
It is a technical eye study, not complete artwork. The new poster Chromium receipt
must still be collected. Save new format-13 work to a separate new file; the
previous guarded reader rejects it, but arbitrary ancient readers cannot be made
safe retrospectively by writing a new file.

Next: collect current full CI and actual poster GUI receipt; validate edge coverage
through Qt and browser; publish only source/tests/docs; optimize cropped ORA leaf
buffers; continue truthful complete-artwork quality review. No private references
or credentials are included in source commits, no PR merge or PR9 changes.

## Cropped integration CI correction

`b680d6d` compiled on both Linux platforms.74/75 tests passed, including allfour
new cropped storage/renderer/ORA/Qt Session tests. The sole failure was the old
B08.MergedDocument memory-accounting contract: ROI cache overhead was mistakenly
charged to every legacy node (384 instead of128bytes). The correction charges
extra256bytes only to actual cropped-layer cache entries. The old test assertion
is unchanged and passed when directly compiled/executed locally. Cloud CI passed;
GUI jobs were skipped by the native failure, so they remain unverified pending
this correction's run.

## Cropped-stroke integration: source ready, new CI pending

Added a separate cropped MyPaint stage and DRAW-DELTA-13 floor. Old raster/native
formats keep their meanings. Native API validates document/revision before reads,
PNG/ROI/manifest placement, PNG hash, global/local sample equivalence, dab guard
receipt and worker document identity;1Mi ROI /2048axis and256Mi aggregate resources
are preflighted. Save-copy/mixed backup/journal guards preserve old projects.

The cloud child plans a conservative102px pointy-ink support margin, checks actual
dab support at every callback, and rejects the entire stroke if an interior ROI
edge is exceeded. Only actual canvas edges may crop. It never downsizes the canvas.
The child retains10s CPU/15s wall/256Mi limits; global inputs, ROI, runtime/preset/
worker/helper hashes are preserved. Oversized strokes fail rather than lose ink.

Native merged renderer and ORA use ROI placement. Immutable parsed views avoid
per-tile hashing and cannot cross snapshot identities. ORA still exports full-size
leaf PNGs with correct placement, not cropped PNG+x/y leaves. New-layer pixels
remain locked raster content, not point-editable native strokes.

Locally VERIFIED after integration: strict C++ format13/12/legacy storage tests;
real3300x5100 renderer with cross512 placement, Multiply/opacity/visibility and
1024x1024 ROI; actual libpng/ORA independently decoded and recomposited. Real
libmypaint child and HTTP-to-native-stub tests passed. A found optimization-mode
bug (`assert` removed preset loading) is corrected with explicit checks; actual
normal versus PYTHONOPTIMIZE=1 output pixels agree and contain ink.

NOT YET VERIFIED: Qt native append_brush_crop RPC and complete new app chain.
New CTest and actual Chromium tests cover512x512 then3300x5100 small local strokes;
they require the next CI result. Native local tests do not imply they passed.
Artwork is still a partial eye study, not the user's finished cover/comic.

## Verified optional MyPaint GUI checkpoint (2026-11-12)

Code `e5a845e5d92c6fec061bbbc218ef27978abf9931`, run43208963274:
all8 Linux/native/runtime-install/Chromium/cloud-session jobs passed on Ubuntu24.04
and Debian13. Native CTest71/71; cloud run43208963249 passed too.
The new MyPaint UI was actually exercised by Chromium: 512x512 canvas, two mouse
strokes, two independent locked Multiply raster layers, undo/redo, saved .draw
with replay metadata, download/reimport pixel equality and PNG/ORA downloads.
This is a technical eyelid-curve test, NOT artwork quality or poster-size proof.
Owner-approved temporary CI runtime used official distro packages; Debian's
libmypaint1.6.0-4 matched the pre-existing default library digest. No user-machine
package installation or settings change occurred. License/preset notices remain.

The actual screenshot exposed a UI reporting defect: raster content was labeled
"empty" because only native events were counted. Current text distinguishes
native events from raster layers; it does not pretend raster marks are point-editable.
Actual row-expression and pointer handler tests passed locally; this text-only fix
has not yet completed a new Chromium run.

Next in an isolated local mirror: cropped MyPaint strokes on full3300x5100 canvas,
format13 old-reader protections, actual-dab fail-closed bounds and ROI-aware native
renderer/ORA. Core/storage/renderer and real library tests pass, including1Mi ROI
and old-reader refusal despite a valid old backup. Qt Session and whole HTTP/native
chain are not yet verified or published. Existing app MyPaint limit remains1Mi
canvas pixels; do not claim the crop prototype has shipped.

## Persisted baseline: cfb92a149664810305febe8c346f6a1e4bd8b0e2

- PNG lineart imports at its original dimensions with untouched original pixels
  and encoded resource. A locked Multiply ink layer sits above independent color.
- Native single-region fill, undo/redo, separate-copy save and reopen are verified
  on the actual historical 1536x1024 bat lineart (7,085 changed pixels confined to
  one face). It is a workflow sample, not the fox or the current assignment.
- 512-tile merged rendering and streamed independent ORA leaves operate at the
  actual target dimensions. Packaged engine passed 1725x2625 / 300 DPI PNG and ORA.
  This test size does not settle the assignment's trim/bleed specification.
- GIMP 3.0.4 actually imported the ORA through its console interface: three layers,
  Multiply and 300 DPI. Hiding color reproduces the input pixel-for-pixel. Restoring
  color differs from native composite at 1,080 pixels by at most 1/255/channel.
- GitHub run 43192748407 built/passed native and runtime-install jobs on Ubuntu and
  Debian. All four browser/cloud-session jobs failed the same screenshot-equality
  assertion after adding an empty layer. No new browser pass is claimed.

## Persisted fine-pen increment

- Add opt-in engine-10 fineliner-aa and mouse/pen pointer entry, 0.05–8 document px,
  one stroke per undo action. Old engines 1/2 are replayed unchanged.
- Add version-10 storage floor with mixed-version overwrite/backup/journal guards.
- Compare decoded native preview pixels in browser acceptance instead of CSS
  screenshots susceptible to subpixel layout resampling. Retain real screenshots
  as separate visual evidence. The new run is still required.

## Native poster / transport increment

11x17 inches at 300 DPI is 3300x5100, 16.83 million pixels. The prior 4096-axis
and 64MiB single-layer limits could not support it. The new bounded path allows
8192 per axis and 20 million total pixels, 128MiB merged/layer buffers, and 320MiB
ORA working capacities. Work/index/geometry ceilings are unchanged. Source
snapshots are additional memory; these limits are not a total process-RSS promise.
Only resources larger than 64MiB require file floor11. Their total resource bytes
are capped at 256MiB, each resource 128MiB, project file still 512MiB. Legacy small-
resource collections retain their existing compatibility.

Actual local C++ execution produced a 3300x5100 three-layer ORA, with a one-pixel
black/gray imported guide retained exactly, lower cyan layer, 70 native tiles,
v11 resource codec roundtrip and mixed-file safeguards. Independent PNG/layer
recomposition passed. This is direct-size technical evidence, not the artwork.
New canvas UI accepts explicit dimensions and shows native size/PPI. Browser
.draw uploads stream in 1MiB chunks up to 256MiB; export/download streams retain
an immutable temporary file until response completion, avoiding whole-file RAM
loads. PNG uploads stream up to 128MiB. Native storage still accepts 512MiB, so files above 256MiB require direct
native access rather than this browser import.

Pen pointer events now retain device pressure (mouse remains constant pressure),
and an empty coalesced-events array no longer drops ordinary movement samples.
At commit35cf74c19f542e9799cf9f17092f6bab917d2346, run43197707147 completed
all eight native/browser/cloud-session/install jobs successfully; cloud runtime
43197707069 also passed. Native67/67 includes B12.PosterNative, independent
ORA checks and PosterSession at3300x5100. Browser scenarios remain512x512
technical lineart/fine-pen tests, not every poster-size GUI operation. Artistic
quality is unproven.

Reference-driven cover experiment: complete original-size 1024x1536 region trace,
no embedded bitmap resources, separate semantic zones. It is not an original
redrawing or recovered high-resolution detail. v3 retains facial expression and
broad/thin hierarchy but has quantized/grainy shadows and red; semantic boundaries
are approximate and title lettering remains knockout pixels, not editable text.
It is rejected as a finished high-quality/print-ready work.

## Optional MyPaint integration checkpoint

`eafc3568e4b0f9071750dc7970d752d525d82126` adds the actual optional UI, pinned
worker and native append-layer RPC. A downloaded verified Debian native binary
was exercised locally through real HTTP handlers, actual MyPaint library and
native session: four independent transparent layers, undo/redo, save-copy/close/
reopen equal pixels, preserved replay manifests, and ORA. This was not a stub or
GUI screenshot. Actual scene was technical eyelid paths, not completed artwork.

The next small follow-up removes non-rendering revision from persistent manifests
so identical inputs at later revisions reuse resources. Different inputs producing
the same RGBA can still be safely rejected by existing resource identity checks;
a dedicated future raster stage/version is needed to solve that completely.

The user explicitly authorized distro-official MyPaint runtime installation only
inside temporary GitHub CI. Browser jobs provision a new local pins file after
installing explicit apt candidate versions and verifying dpkg ownership/version;
package receipt and hashes are retained. Repository default pins are unchanged;
HTTP cannot specify pins. Existing saved manifests are never silently migrated.
This enables real Chromium testing of the optional brush without installing on
the user's computer. First-tool limits remain black ink, locked raster layers,
1,048,576pixels/2048axis; poster-size MyPaint is NOT supported yet.

## Current persisted checkpoint and next opt-in integration

`00c203f07dd0740a34370aabccbd9851f9b8e304` persisted the experimental dry-ink
increment below. `38a3b94009f3ad2af16e69e6d84a56dd34c19bce` changes only the
JPEG ICC test fixture construction to fixed-size byte-identical data: GCC13
-O3 had warned on the previous vector growth under -Werror. Warning checks were
not disabled. Run43203822219 completed all eight Linux build/browser/cloud-session/install
jobs successfully; Debian native69/69 includes the dry-ink tests. Cloud runtime
run43203822168 passed. These results do not establish artistic quality.

The user requested immediate optional MyPaint integration. The initial contract
is a separately identified experimental black-ink brush. Each new stroke becomes
an independent locked Multiply raster layer with immutable original PNG and
replay manifest resources; native undo/save-copy/reopen must retain them. This
is NOT native point-editable vector stroke support. Existing tools/files must
not be silently reinterpreted. Local use requires an available matching runtime;
no dependency availability or cross-platform determinism is assumed.

Local independent work (not automatically production features): optional input
stabilization passed mock-DOM tests but no live-browser hand-drawing acceptance;
original raw samples are only in-page/downloadable before close, not persistent.
Real libmypaint ink presets produced same-host repeatable pixels; version/preset
pins, process limits, cancellation, expected-pixel check and no-overwrite commit
were exercised in an isolated adapter. All face drawings still fail final quality:
better brushes do not fix inaccurate proportions or black/white construction.

## Experimental dry ink and retained visual failures

The next opt-in engine12 brush keeps editable stroke geometry, persistent seed,
pressure-controlled width/ink, nonuniform bristle intervals and continuous ink
supply along the path. A single closest-centerline frame avoids max-combining
incompatible segment textures. Additional32bytes/pixel nearest records and five
bristle visits/pixel are included in existing memory/work budgets. Mixed old/new
file protection uses floor12. Older brush engines retain their previous pixels.

The v4 same-path rendering removed the preceding conspicuous grid/moiré, but is
still an experiment: fine/medium marks resemble parallel wires and lack natural
bristle clustering and broken ink edges. The UI labels it an experiment. No
artist-level/full-cover acceptance is claimed. Native test, builder and storage
checks passed locally; full current bridge/browser CI is still required.

Manually authored eye studies are retained in tools/artwork. Independent visual
review rejected v2 and v3: the pupil improved, but hair shapes were blocky/ribbon-
like, proportions differed and stroke quality remained mechanical. Do not expand
those studies across the whole artwork. Their reproducible source is a failure
record, not approved art. The private reference image is not uploaded to GitHub.

Selected-layer naming/opacity now use native undoable commands, and the browser
layer list displays frontmost first instead of reversing the perceived ink/color
stack. A caller-specific encoded PNG preflight cap matches the128MiB upload cap;
other import callers retain the existing64MiB default.

## Next steps and remaining gaps

1. Run exact current Session/API/browser fine-pen and lineart workflows, fix failures,
   and publish before/after/undo/reopen screenshots from the real running version.
2. Use the user's red DELETE references and existing story for complete-art work.
3. Improve AA fill edges/gap handling; neither is currently implemented.
4. ORA groups are flattened, masks baked, locks omitted. Keep .draw for full edits.
   PSD group/mask fidelity is not implemented.
5. Native cloud engine + browser editor are the executed path. Windows desktop GUI
   and final artist-quality output remain unverified. Never merge PR9 or this draft
   without authorization. This is reconstruction, not recovery of lost source.

The chronological notes below describe prior checkpoints and known failures at
those points; the current status above supersedes their pending/completed labels.

---

# Bounded bucket selection reconstruction

This is a newly reconstructed small component based on PR #7
f9085f9934b5f0c2dba42b0433ecde15c862a794, not recovery of the inaccessible
v3-v8 repair source.

Implemented: a native C++ read-only flood-fill selector returning disjoint,
row-sorted runs. It uses 4-connectivity, seed-relative per-channel tolerance,
transparent-RGB equivalence, bounded canvas allocation and run count, and
cooperative cancellation. No partial mask is returned on failure.
The default supports the 3300 x 5100 print canvas and caps input at 32 Mi pixels
and output at 65,536 runs. The caller must provide a rendered RGBA8 reference;
the input itself is never changed.

Follow-up integration adds a native v8 bucket event, exact pixel-span coverage,
undo/redo through Document, merged-reference selection, a separate color layer,
browser click handling and a save-copy flow. New snapshots containing v8 use
DRAW-DELTA-8. Unknown versions fail closed; mixing legacy and v8 primary/backup/
journal or overwriting across formats is rejected. Already released old readers
are not repaired. Keep newer projects in separate copies.

Still unverified: actual Qt session compilation, full native session test and
real GUI. Not implemented: antialias gap closing or masked-target fill (rejected),
or current-layer reference selection (merged visible image only). The existing
full-document renderer still has its original 4 Mi pixel/layout limits; the
3300 x 5100 selector test does not make the GUI support that canvas size.
The former application's GUI/58-of-60 report does not validate this reconstructed
code. Artist-quality drawing is not established.

Fresh validation:
- Test-first basic enclosed-region assertion failed on the explicit unimplemented
  function, then passed after implementation.
- 3300 x 5100 default-budget regression failed before the default was corrected.
- Native tests cover border seeds, disconnected diagonal pixels, malformed
  dimensions, pixel/run budgets, cancellation, tolerance, transparent pixels,
  input immutability, 500 seeded random images against an independent BFS oracle,
  and a 16,830,000-pixel print canvas.
- Compile/run directly with:

  g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I src tests/brush/test_flood_fill.cpp src/brush/FloodFill.cpp -o /tmp/test_fill
  /tmp/test_fill

- CMake registers B08.BoundedFill, but the full CMake build/CTest suite cannot
  run in this reconstruction executor: CMake and Qt development tools are absent.
  No new desktop tools were installed.
- Address/undefined sanitizer run uses ASAN_OPTIONS=detect_leaks=0 because this
  executor's ptrace environment prevents LeakSanitizer from running. Leak
  checking is not claimed.

Additional fresh validation: native bucket event apply/render/undo/redo and
malformed-run/lock/visibility/transform guards pass with ASan/UBSan (same LSan
limitation). Storage tests cover format roundtrip, unknown-primary no fallback,
bidirectional old/new overwrite rejection and mixed/orphan sidecars. Original
F04 recovery suite passes 14 cases. Six Python API transport tests and JS UI
helper/click tests pass, but their transport stubs are not native acceptance.
Python tests emit an existing Starlette/httpx deprecation warning.
A real headless session test is registered for CI; it has not run locally.

Keep subsequent integration on this isolated branch. Do not overwrite PR #9
or merge this partial component as if the full repair were complete.

## Resume checkpoint (2026-11-12)

Goal: a genuinely usable, editable, layered native drawing workflow. This bucket
reconstruction is one component, not a claim of artist-level drawing or recovery
of the lost prior repair.

Persisted checkpoints on PR #10:
- f6e4d526: bounded selector + tests, six remote files read back exactly.
- 88b3ddf2: v8 event/renderer, session bridge, UI/API and format-safe save-copy;
  branch head confirmed and Session/Store/UI read back exactly.

Verified locally: selector, real native event rendering/undo/redo, storage safety,
14 original F04 recovery cases, six stubbed-transport API tests, JS click guards.
Qt bridge and full suite are NOT locally verified. No current real GUI acceptance.
Commit dd856042 adds the layer-budget preflight and expanded storage checks.
GitHub run 43180485075 built Ubuntu and Debian native targets. Ubuntu executed
51/51 CTests successfully, including B08.BucketSession (actual native fill, undo/redo,
save-copy, reopen, PNG/ORA checks). Both jobs subsequently FAILED evidence inventory
validation: B08 tests were registered after the inventory snapshot. This checkpoint
moves the include after B08 registrations, retaining the strict evidence assertion.
The real browser jobs were SKIPPED because the prerequisite jobs failed, not passed.
Cloud runtime run 43180485104 passed. Native pipeline rerun remains required.

Next work, in order:
- [ ] Read CI for the exact latest code SHA and fix real build/regression failures.
- [x] Run the real BucketSession test and inspect actual saved/loaded pixels (Ubuntu CI).
- [ ] Test actual browser fill, undo, redo, separate-copy save, reload and download.
- [ ] Confirm file delivered and layers independently editable; document limits.
- [ ] Revisit fine brush quality, semantic layers and original-art quality separately.
- [ ] Preserve each runnable increment remotely. Do not overwrite PR #9 or merge.

Current limits: no masked-target fill or gap closing; merged visible reference only;
original full-document 4 Mi pixel renderer budget is unchanged. Existing released
old readers cannot be patched by these files, so new-version copies remain separate.

Independent review found that adding a layer could exceed the current preview
memory budget after mutation. A real native regression reproduced this at 2048²
with three existing nodes; the new preflight now rejects a fourth node before
mutation. The test is GREEN. This deliberately does not raise rendering limits.

## Merged-only print-size increment (pending new CI)

At 85f3383d, run 43183043870 passed native build/51 CTests/46 Python
checks/evidence collection on Ubuntu and Debian, plus both runtime-install jobs.
The same SHA's downloaded Debian engine passed BucketSession again in this cloud
workspace after artifact SHA256 verification. Dedicated browser jobs failed before their Chromium exercise because older image
mocks lacked HTMLImage.decode. The separate cloud-session jobs DID execute Chromium
and reached bucket fill, then returned 422: the real fixture has 4418 strokes and
the new bucket cap was 4096. This increment repairs image mocks, asserts the decode
gate, and raises the tested event cap to 8192 while retaining geometry/work limits.
The existing 4418-event .draw passes the new merged renderer locally. Small-canvas
coverage accounting now uses actual clipped tile dimensions, not 512 squared for
every tile. Neither work nor allocation budgets were increased. Four Node suites
pass locally. No successful complete Chromium fill acceptance is claimed yet.

The next increment introduces a separate merged-only 512-tile renderer for PNG and
bucket references. It does not retain every full-resolution layer. Local strict
and ASan/UBSan tests cover old-renderer exact pixels, paper/opacity/hidden layers,
soft layer and binary ancestor masks, tile edges, cancellation, stale revisions,
null pointers, and checked memory/index/geometry/work budgets. An independent
review reproduced null-pointer crashes and missing ancestor-mask scan work before
both were fixed. A 1725 x 2625 native pixel test passes; this is an engineering
test target, not confirmation of the assignment's trim/bleed specification.

A local unoptimized full test suite measured 27,828 KiB child-process peak RSS and
1.803 seconds before the additional review regressions. This is one suite sample,
not a universal memory/performance guarantee. The 64 MiB renderer allocation model
excludes existing snapshot resources, later bucket RGBA/visited buffers and encoded
output; work/index limits are checked independently. Session create remains capped
at 4096 per axis and now uses the renderer preflight rather than a blanket 4 Mi px
area limit. Bucket/new-layer candidate state is preflighted before real mutation.
Legacy callers retain the old add-layer allocation rule.

New real-engine PrintBucketSession covers 1725 x 2625 PNG pixels and 300 DPI,
fill/undo/redo/save-copy/reopen, and explicitly expects ORA at that size to remain
unsupported. It was run against 85f3383d and failed at session.size (RED), as expected.
The new bridge is not locally built; await new CI for GREEN. ORA/PSD streaming,
external-editor layer validation, and art quality remain unfinished. Do not infer
large layered export support from a large PNG or from the native .draw event file.

## PNG lineart construction increment (pending new CI)

User correction: the target is an original lineart above an independent color
layer, sampling across layers and filling one closed area. A whole colored-picture
overlay is not acceptance of that goal. Run 43185600900 for 7e836803 passed every
native/browser/cloud-session/install job, but its older colored-picture bucket
scenario does not satisfy this corrected workflow.

Implemented now: Open project accepts PNG lineart, bounded native PNG decoding,
original encoded PNG and decoded RGBA retained byte-for-byte as resources, a locked
Multiply lineart layer, and a new color layer inserted below ink. Normal/Multi-
alpha rendering keeps the original resource unchanged; no thresholding, tracing,
thinning or image-generation model is used. Non-sRGB embedded profiles are rejected
rather than silently changing color. Pixel dimensions are retained; document PPI
is initially 300. Other raster import types are not implicitly enabled.

Native 512x512 technical two-region test fills only the left area, with the other
region/outside and original ink layer unchanged, plus undo/save-copy/reopen. This
is NOT the user's artwork. No antialias expansion/halo removal or gap closing is
claimed. The historical bat lineart is being used as a separate real-input sample;
it is not called the fox and not represented as the current assignment.

The new raster feature requires DRAW-DELTA-9. Legacy/v8/v9 crossing, mixed backup
or journal, unknown headers and mislabeled feature headers fail closed; save new
copies instead. v9 can contain bucket-v8 events; no stroke engine 9 was invented.
Small native resource/alpha and storage tests pass. The new actual PNG entry and
new closed-region Chromium exercise still require the next CI. Layered ORA with
imported raster is explicitly disabled, pending the separate streaming export step.

## Streamed layered ORA construction checkpoint (pending next CI)

The opaque RGB PNG import fix reached 56/56 native CTests. A later isolated
experimental g++ link omitted the new lineart parser object; the immutable parser
is now header-inline, matching the existing inline layer checks, rather than
requiring every independent experimental script to link an unrelated builder.

Implemented actual layered export: NativeLayerStream produces one layer at a time,
OraStream writes each real PNG immediately via bounded ZipStream and atomically
publishes the archive. Multiply lineart, lower color layer, visibility, opacity and
paper are represented as independent ORA leaves. Original resources remain
unchanged. Groups flatten to leaves, masks are baked into exported leaf pixels,
and locks are not retained; the UI explicitly says to keep .draw for full editing.
This is not PSD group/mask round-trip fidelity.

Bounded PNG buffers reserve their cap before compression and check writes and
cancellation; output bytes, ZIP32 offsets, entries and metadata are limited.
Stage cleanup is armed only after exclusive creation. Pre-publication failures
retain any existing destination, including a competing creator. A post-publication
fsync failure can still mean committed-but-not-confirmed, as in existing FileIO.

Real local system-libpng execution passed: 1725x2625 three-layer ORA, independent
ZIP/PNG CRC/decode, 300DPI and ICC bytes, Multiply/visibility/opacity recomposition,
existing-target refusal, cancellation/provider/write limits and stage cleanup.
Strict and ASan/UBSan checks passed. No codec library was installed; three official
libpng compile headers used locally are NOT included in the repository.

An actual editable historical-bat result was also produced: 1536x1024, 300DPI,
Original lineart (Multiply) / Color fill / Paper. Original ink RGBA is byte-exact;
independent re-composition equals the native filled result. ORA SHA256:
176cb296c6445ecd9f63ed1fd4f30a4216baef02c56b36b29fa2c427390f9239.
It has not been opened in an external GUI. This is historical bat evidence, not
the fox or the current assignment. Full current UI/session CI is still required.

Next: collect exact latest CI and new closed-region browser screenshots; run the
actual bat PNG through the packaged UI; verify external-editor layer behavior;
then improve fine-line/clean-brush output using whole-scene visual inspection.
