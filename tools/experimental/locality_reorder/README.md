# Native dependency-preserving stroke locality, experimental

This standalone CLI is **default-off**. It is not linked from the editor,
headless application, automatic stages, flow planner or hybrid planner.
It reads an existing native `.draw` project and writes a separate reordered
native project. It does not generate new strokes, infer artist intent,
change pixels or improve image quality.

## What is preserved

The actual native `strokeBounds` function supplies conservative bounds,
including nib radius and the engine's margin, clipped to the canvas. Every
pair of strokes with intersecting bounds keeps its original order. A compact
bitset dependency graph allows other strokes to move closer to the previous
stroke's endpoint. Scheduling never crosses original contiguous
phase/layer/object blocks or the declared immutable prefix. Strokes are not
reversed or resampled.

Nearest-ready scheduling is greedy, not a global minimum. The candidate is
retained only if total geometric pen-up distance does not increase, including
all block and phase boundaries. Otherwise the original order is retained.
Distance is a property of stored coordinates, not measured UI pointer travel.

The native helper independently validates the permutation and all conservative
overlap dependencies. It creates a fresh immutable event index and revision.
IDs are regenerated after the protected prefix, and absolute timestamps are
translated. All other event fields, internal point-time offsets, duration,
pressure, geometry, color, opacity, widths, hardness, shape and grain are
preserved. Node and resource contents are unchanged. The original 80,000 us
inter-stroke gap is retained, so less travel does not imply faster playback.

After saving and reopening, native final, every layer and every phase-end
image must be **byte-exact** to the input. A one-byte difference fails the
operation. Fresh/warm renderer and hide/show restoration checks are included.
Intermediate time/prefix frames intentionally differ. The helper also emits
start/mid/end native timeTicks samples; these are not a complete movie or a
UI/live recording. No source raster is embedded or read for replay.

## Supported input and resource limits

- Existing reconstructed RGB U8 alpha projects, 1..512 pixels on each axis.
- 1..8,192 events, at most 500,000 samples and at most a 128-pixel nib.
- Only the `base`, `form`, `detail` stages, monotonically ordered, at most
  three continuous stage runs. At most 16 objects and 48 layers.
- Supported normal, untransformed native layers; Mask-only resources with
  same-canvas binary masks. No imported raster/reference/tile resources.
- Uniform 80,000 us gaps and valid per-stroke timestamp intervals.
- At most 32 MiB input project, bounded permutation and declaration reads.
- 256 MiB stored graph budget, **not** a total process-memory limit, and a
  120-second cooperative scheduling deadline. Native operations have separate
  bounded subprocess timeouts; one native render is not cooperatively stopped.

The native helper rejects excessive stage/layer/output budgets before creating
its native artifact directory. The conservative output estimate is limited
to 128 MiB and 128 files. It reserves 32 MiB for the native project, 1 MiB
for headers/manifests and 2,048 bytes per event for metadata/mapping, plus
bounded full-canvas evidence images. These are output/cache bounds, not a
process-RSS limit. Python may already have created the enclosing run directory
and will mark it failed if native preflight rejects the input.
The contract is deliberately narrower than all projects the application can
open. It is not a universal untrusted-file conversion tool.
Source photos, large example projects and videos are not included in this
package or run by CI.

## Immutable prefix declaration, required

The tool cannot infer which strokes form an artistically important primer.
Do not guess a prefix count. Inspect the project's verified planner/source
record, or explicitly determine **the entire prefix that must remain
unchanged**. If you cannot determine it, do not reorder that project.

The operator creates a declaration with that explicit count. It binds the
input file SHA256, recovered native state SHA256, event count and immutable
prefix count. The declaration is not permission to change the prefix, nor
proof that the tool understands a semantic primer. Selecting its complete
extent is the operator's responsibility. Counts must be 1..event count;
zero is intentionally unsupported in the public interface. Declaring the
entire event list gives a safe no-change order.

Declaration format is one canonical ASCII line:

    DBLOCALITY1 <file_sha256> <native_state_sha256> <event_count> <immutable_prefix_count>

Both Python and native ingress verify it. A declaration cannot be silently
reused after the source bytes/state/count change. The frozen v5 research
example used a separately verified 11-event prefix; **11 is not a default
for arbitrary input**.

## Build and use

Requirements: C++20 compiler, Python 3 and NumPy. Existing Linux CI already
installs distro NumPy; no pip installation, new library, model or service is
required. Build from the repository root:

    bash tools/experimental/locality_reorder/build.sh

The helper is built directly against the repository's existing native core;
no stable core files are copied or changed. An optional output binary path
and `DRAWBORAD_SOURCE` allow an isolated build. The normal default resolves
the repository root relative to the script, not the current directory.

Every operation requires the explicit research flag:

    python3 tools/experimental/locality_reorder/cli.py --research inspect --input project.draw --output new-inspection
    python3 tools/experimental/locality_reorder/cli.py --research declare --input project.draw --immutable-prefix-events N --output prefix.txt
    python3 tools/experimental/locality_reorder/cli.py --research reorder --input project.draw --declaration prefix.txt --output new-result

Use `--native PATH` before the subcommand or `DRAWBORAD_NATIVE_LOCALITY` to
select a separately built helper. An explicitly missing runner fails.
Direct native usage has the same gate and declaration requirement:

    native-locality --research declare project.draw N prefix.txt
    native-locality --research reorder project.draw permutation.txt prefix.txt new-native-result

All output paths must be new; parents must already exist. The CLI rejects
existing destinations, input/output aliasing and symlink paths, including
dangling symlinks. It does not overwrite the input or remove old output.
It rejects `.bak` and `.journal` recovery sidecars instead of silently
recovering an alternative state. Open and save a clean separate project
copy with the application first if recovery is needed. Native ProjectStore
may create/use its normal `.lock` coordination sidecar; this is not a
promise of absolutely zero filesystem writes beside the input. Existing path
components are checked for symlinks; input and output parent directories must
not be concurrently renamed/replaced by another process during the run.

## Output and failure semantics

`new-result/native/reordered.draw` is the new native document.
`permutation.txt`, `schedule.json` and `native/mapping.json` explain the
old/new order, regenerated IDs and absolute timestamps. Native layer,
phase-end, final and time-sample images are separate evidence files.

Only `result.json` with `status: completed`, a zero exit code, and complete
native success checks denotes success. An exception or timeout returns
nonzero. A failure after work starts may leave a directory containing
partial artifacts and a failure report. Do not open/share a partial project
as a successful result and do not blindly retry into that directory.

## Tests and CI

From the repository root:

    bash tools/experimental/locality_reorder/build.sh "$PWD/out/locality-reorder/native-locality"
    DRAWBORAD_NATIVE_LOCALITY="$PWD/out/locality-reorder/native-locality" python3 -m unittest discover -s tools/experimental/locality_reorder -p 'test_locality.py' -v

CI adds only this isolated step to the existing Ubuntu 24.04 and Debian 13
native-build matrix. It uses that matrix's system Python/NumPy stack and
does not install a competing NumPy wheel. The tests create small synthetic
native projects in temporary directories, including transparent/opaque
backgrounds, AA/soft contacts, fixed grain/Chisel, noncommuting overlap
negative controls, boundary/prefix protection, timing, fresh caches,
save/reopen, canonical declarations, sidecars, no-clobber and failure status.

The packaged local suite has 20 Python tests, including a native self-test
with 69 assertions (the original 44 plus 25 contract checks). A missing
default local binary may skip native integration tests, but an explicitly
configured missing binary fails. CI always supplies an explicit binary.

Local compilation and this focused suite were verified in the cloud research
workspace. The new remote Ubuntu/Debian CI step has not yet run. Previous
green CI belongs to the unchanged base commit, not this new patch.

## Evidence and limitations

On one already-seen frozen v5 photograph, the prototype reduced total
geometric pen-up from 1,037,522 to 201,454 pixels (80.58%), without changing
the final/layer/phase-end pixels. Detail median displacement fell from
126.01 to 10.52 pixels. Whole-plan jumps over a predeclared 100-pixel threshold
fell from 4,743 to 304. This is one research result, not a promise for every
project. Conservative dependencies can force the original order.

That example still has 7,314 events and the original image/texture defects.
Exact final pixels preserve its quality failures too. This can make the
stored process spatially more continuous; it does not establish meaningful
artistic layers, human drawing intent or a semantic painting workflow.
