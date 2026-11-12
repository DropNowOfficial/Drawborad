# Optional MyPaint pointy-ink backend data

The cloud worker uses only preinstalled libraries whose SHA256 matches pins.json.
No download/install or fallback occurs. If absent/mismatched, MyPaint is disabled;
existing native brushes remain unchanged. This is a separate experimental raster
backend, NOT engine12. Each stroke appends a distinct locked Multiply layer with
its replay manifest, rather than modifying a selected existing layer.

Official preset: https://github.com/mypaint/mypaint-brushes/blob/master/brushes/classic/pointy_ink.myb
Git blob:8b9afc958ea51e0a6aebc727106318ddb9f92d34 (SHA256 in pins.json).
Preset radius/opacity/input mapping unchanged; only color set black. Width cannot
be mapped directly from native brushes because the preset's pressure mapping is
different. UI width/color controls are disabled for this brush.

Copied license originals: libmypaint-COPYING (ISC permission and notices),
brushes-Licenses.dep5 (canonical file-specific coverage), brushes-COPYING (CC0),
brushes-Licenses.md. Selected brushes/classic/*.myb falls under brushes/* CC0;
other repository code defaults GPL-2+, not wholesale CC0. ABI wrapper is locally
authored from public v1.5.1 declarations. Existing binary redistribution and full
dependency licensing are not authorized/solved by this prototype.

Limits: current canvas unchanged, ≤2048/axis, ≤1,048,576 pixels, ≤1024 points,
≤60s input timeline, worker256MiB AS/10s CPU/15s wall, parent cancellation20ms
poll. Backend render runs in a subprocess and no-clobber temp output publication.
Cancel is accepted only before native commit begins. UI busy state is not used as
security: server transaction and revision+document-ID revalidation guard append.

Worker reads original MyPaint opaque white grayscale output; it rejects nongray
or nonopaque source and converts to black RGBA with alpha255-gray. Recomposition
over white is byte-identical; empty pixels are transparent, not an opaque cover.
Canonical manifest contains runtime/preset/ABI/worker hashes, all x/y/pressure/
time data, original-canvas dimensions, color/size policy, interpolation/timing,
RGBA output hash and conversion semantics. PNG tEXt DrawboradReplaySHA256 stores
that canonical manifest hash so distinct inputs can have distinct resources even
when pixels match. Manifest omits encoded PNG hash to avoid a cyclic dependency;
worker stdout receipt carries encoded PNG and manifest hashes for transport check.

Same-machine real-worker repeatability and transparency tested. Cross-platform
repeatability, full dependency closure pinning, native-RPC/GUI integration must
be separately verified by the new native CI build; transport mocks are not proof
of end-to-end functionality. No artwork quality or human-artist equivalence claim.

Mouse defaults topressure0.35 with an explicit0.05–1 control, avoiding the preset's
maximum radius by default. Actual stylus pressure is retained. Fine/dry native
brushes keep their existing mouse behavior. Pointer-up samples are retained even
when movement is tiny; the complete stroke is one append/undo operation.

Cloud native state dimensions are decimal strings; the optional worker validates
and converts them internally only, without changing the state API. On non-Linux
or missing resource/lib/PIL dependencies, optional availability is false rather
than breaking module import. RLIMIT is applied inside the worker CLI before ABI/
PIL loading, not via preexec_fn in multithreaded FastAPI.

Tests currently cover real installed-library worker, transparency/white equivalence,
repeat byte equality, cancellation, HTTP-to-native boundary using a labeled stub,
and actual page pointer code in Node. New append_brush_layer native CI acceptance
is still required for the complete user workflow. No private reference images or
user documents are part of this feature's data directory.

Persistent manifest identity excludes expected_revision: revision authorizes the
append in the temporary request and native commit, but does not affect rendering.
Identical rendering inputs in the same document at later revisions therefore
produce identical canonical manifest and encoded PNG, allowing resource reuse.
All rendering inputs, dependency hashes, timing and parameters remain retained.
Different input sequences that happen to produce identical RGBA may still be
safely rejected by native dependency collision checks; this does not eliminate
that case and does not claim complete deduplication of equivalent renderings.

## Explicit ephemeral-CI first-time pins provisioning

An operator may configure the server process with the absolute local file path
`DRAWBORAD_MYPAINT_PINS_FILE`. HTTP/request bodies cannot supply this setting.
Absent the environment variable, packaged pins.json is used unchanged. Overrides
must retain exactly the four64-hex keys libmypaint/json_c/preset/abi. A changed
library still fails unless explicitly configured with a matching first-time pins
file; saved replay manifests are never silently migrated or retrusted.

The user authorized distribution-official runtime installation only in disposable
GitHub CI, not on their computer. Installation remains the workflow operator's
separate action. `tools/experimental/mypaint/provision_ci_pins.py` does not install
anything and requires both GITHUB_ACTIONS=true and --allow-ci-provision. Example
following authorized exact-version apt installation:

```
GITHUB_ACTIONS=true python3 tools/experimental/mypaint/provision_ci_pins.py \
 --allow-ci-provision --output /app/mypaint-ci-pins.json \
 --receipt /app/mypaint-ci-receipt.json \
 --expected-libmypaint-version "$MYPAINT_CANDIDATE" \
 --expected-json-c-version "$JSON_C_CANDIDATE"
```

It verifies dpkg-query installed status, exact requested versions and ownership of
the actual loaded library files; records actual package versions/architecture and
/etc/os-release in a separate receipt; writes the original four-key pins schema.
It refuses to update any existing output or repository default pins. Configure
file ownership/read permissions for the intended non-root worker explicitly.
A failure publishing the receipt after pins must fail the CI setup, not be treated
as success. Package origin/signature verification belongs to the official apt
installation step; dpkg inventory alone is not an authenticity proof. This is an
explicit fresh CI configuration, not an automatic fallback for saved replay hashes.
