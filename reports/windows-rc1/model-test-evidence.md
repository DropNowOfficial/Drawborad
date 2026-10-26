# Model preflight implementation and test evidence

This is focused model-subsystem evidence for the uncommitted RC1 working tree based on
`69504ea037bf3a30c7a7afae2f19dc8742791610`. The final root CTest runs are the authoritative
whole-branch verification. These isolated runs do not establish Windows acceptance,
real SAM compatibility, segmentation quality, or CUDA qualification.

## Implemented contract

`preflightModelPackage(const PackagePath&, DevicePreference, std::stop_token)` returns
one of `MissingModel`, `InvalidManifest`, `ModelIntegrityFailed`, `RuntimeUnavailable`,
`ProviderUnavailable`, `IncompatibleModel`, or `Ready`. Cancellation remains a
`Result` error rather than a readiness state.

`Ready` requires a verified local package, the runtime/provider, and successful
execution of a fresh encoder and decoder with validated outputs. The report labels
that input `deterministic-smoke`. It does not mean that a user's image has been
segmented or reviewed. A storage-only ONNX fixture cannot become Ready, and a build
without ONNX Runtime reports `RuntimeUnavailable` after package verification.

The verified manifest retains source, source commit, checkpoint digest, variant,
declared network policy, license, provider, task/ABI, both actual weight digests, and
the digest of the exact manifest bytes. The UI/worker integration separately binds
per-image work to the successful receipt's manifest, encoder, and decoder hashes.
No model-weight download path was introduced.

Input dimensions are compared with the actual model interface; only `-1` is a
supported dynamic dimension. Concrete output ranks/dimensions are validated, and
encoder feature shapes are checked against both encoder output and decoder input
declarations. Nonfinite encoder features are rejected before caching, including
features that the decoder ignores. Final float outputs also receive finite checks.

## Verified results

All following runs exited `0`. The host compiler identified itself as GNU C++ 13.3.0.
The runtime SDK was the root-provisioned pinned Linux x64 ONNX Runtime 1.23.2 SDK at
`/tmp/drawborad-rc1-deps/onnxruntime-linux-x64-1.23.2`. Runtime models were generated
from the existing `.github/workflows/cm-ort-runtime.yml` fixture definitions; they
are synthetic ONNX graphs, not SAM weights.

| Configuration / executable | Cases | Failures | Output log |
| --- | ---: | ---: | --- |
| No ORT: `no-ort-model-contracts` | 36 | 0 | `model-test-logs/green-no-ort-model-contracts.log` |
| ORT: `model_contracts` | 36 | 0 | `model-test-logs/green-ort-model-contracts.log` |
| ORT: `sam_runtime` | 9 | 0 | `model-test-logs/green-sam-runtime.log` |
| ORT: `compatibility_runtime` | 4 | 0 | `model-test-logs/green-compatibility-runtime.log` |
| ORT: `single_runtime` | 2 | 0 | `model-test-logs/green-single-runtime.log` |

The ORT configuration therefore ran 51 cases. These overlap with the 36 no-ORT
contract cases; they are not 87 distinct scenarios. The 15 new readiness cases are
included in each model-contract executable. Legacy package, adapter, and provider
tests were retained without weakened assertions.

`compatibility_runtime` executed a fresh two-stage package preflight and observed
Ready with `probeKind=deterministic-smoke`. Its NaN regression also exercised the
case where encoder features are nonfinite but the fixture decoder produces finite
outputs. `sam_runtime` covers wrong output rank, fixed dimensions, invalid negative
and zero dimensions, valid `-1` dimensions, runtime input shape comparison, and
nonfinite decoder output.

## RED evidence

The `red-*.log` files are historical captures transcribed from the recorded tool
outputs, with the original command and exit code attached. They are intentionally
not fresh failures of the completed implementation. No production fixes were
reverted to manufacture these logs.

| Regression stage | Observed failing cases | Capture |
| --- | ---: | --- |
| Invalid provider/ABI/network-policy types silently accepted | 3 of 3 | `model-test-logs/red-metadata-before-fix.log` |
| Exported provenance discarded | 1 of 4 | `model-test-logs/red-provenance-before-fix.log` |
| New state behavior against conservative MissingModel stub | 9 of 15 | `model-test-logs/red-readiness-before-implementation.log` |
| Actual runtime shape validation | 6 of 9 | `model-test-logs/red-sam-runtime-before-fix.log` |
| Nonfinite encoder features accepted despite finite decoder output | 1 of 4 | `model-test-logs/red-compatibility-runtime-before-fix.log` |

## Full verification commands

Working directory for all commands:

```text
/workspace/scratch/f80809aed485/Drawborad
```

The final no-ORT executable was compiled from the actual model/core sources with
the project's warning and floating-point flags:

```bash
mkdir -p /tmp/drawborad-model-readiness-tests/fixtures
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off -pthread -Isrc -Itests/support tests/support/TestMain.cpp tests/models/ModelReadinessTests.cpp tests/models/PackageTests.cpp tests/models/CustomAdapterTests.cpp tests/models/ProviderParityTests.cpp src/models/AdapterRegistry.cpp src/models/PackageVerifier.cpp src/models/ModelRegistry.cpp src/models/OrtSession.cpp src/core/Digest.cpp src/core/DocumentSpec.cpp src/storage/FileIO.cpp -o /tmp/drawborad-model-readiness-tests/no-ort-model-contracts
TMPDIR=/tmp/drawborad-model-readiness-tests/fixtures /tmp/drawborad-model-readiness-tests/no-ort-model-contracts > reports/windows-rc1/model-test-logs/green-no-ort-model-contracts.log 2>&1 && tail -n 1 reports/windows-rc1/model-test-logs/green-no-ort-model-contracts.log
```

The temporary ORT harness at `/tmp/drawborad-model-readiness-tests/CMakeLists.txt`
uses C++20 and the same warning/floating-point flags. Its `models` library consists
of `AdapterRegistry.cpp`, `PackageVerifier.cpp`, `ModelRegistry.cpp`, `OrtSession.cpp`,
`Digest.cpp`, `DocumentSpec.cpp`, and `FileIO.cpp`. It defines
`DRAWBORAD_HAS_ONNXRUNTIME=1`, uses the pinned SDK include directory, and links
the pinned `lib/libonnxruntime.so`. Its executables use the repository's
`tests/support/TestMain.cpp` and the test files named above. It adds no mocks.

Configure and final incremental build commands:

```bash
/tmp/drawborad-rc1-build-tools/cmake/data/bin/cmake -S /tmp/drawborad-model-readiness-tests -B /tmp/drawborad-model-readiness-tests/ort-build
/tmp/drawborad-rc1-build-tools/cmake/data/bin/cmake --build /tmp/drawborad-model-readiness-tests/ort-build -j 3 > reports/windows-rc1/model-test-logs/green-ort-build.log 2>&1 && cat reports/windows-rc1/model-test-logs/green-ort-build.log
```

Final runtime execution commands:

```bash
TMPDIR=/tmp/drawborad-model-readiness-tests/fixtures /tmp/drawborad-model-readiness-tests/ort-build/model_contracts > reports/windows-rc1/model-test-logs/green-ort-model-contracts.log 2>&1 && tail -n 1 reports/windows-rc1/model-test-logs/green-ort-model-contracts.log
TMPDIR=/tmp/drawborad-model-readiness-tests/fixtures DRAWBORAD_TEST_SAM_ENCODER=/workspace/scratch/f80809aed485/Drawborad/out/rc1-runtime-fixtures/sam-encoder.onnx DRAWBORAD_TEST_SAM_DECODER=/workspace/scratch/f80809aed485/Drawborad/out/rc1-runtime-fixtures/sam-decoder.onnx /tmp/drawborad-model-readiness-tests/ort-build/sam_runtime > reports/windows-rc1/model-test-logs/green-sam-runtime.log 2>&1 && tail -n 1 reports/windows-rc1/model-test-logs/green-sam-runtime.log
TMPDIR=/tmp/drawborad-model-readiness-tests/fixtures DRAWBORAD_TEST_PROBE_ENCODER=/workspace/scratch/f80809aed485/Drawborad/out/rc1-runtime-fixtures/sam-probe-encoder.onnx DRAWBORAD_TEST_PROBE_DECODER=/workspace/scratch/f80809aed485/Drawborad/out/rc1-runtime-fixtures/sam-probe-decoder.onnx /tmp/drawborad-model-readiness-tests/ort-build/compatibility_runtime > reports/windows-rc1/model-test-logs/green-compatibility-runtime.log 2>&1 && tail -n 1 reports/windows-rc1/model-test-logs/green-compatibility-runtime.log
TMPDIR=/tmp/drawborad-model-readiness-tests/fixtures DRAWBORAD_TEST_ONNX_MODEL=/workspace/scratch/f80809aed485/Drawborad/out/rc1-runtime-fixtures/identity.onnx /tmp/drawborad-model-readiness-tests/ort-build/single_runtime > reports/windows-rc1/model-test-logs/green-single-runtime.log 2>&1 && tail -n 1 reports/windows-rc1/model-test-logs/green-single-runtime.log
```

The temporary `TMPDIR` prevents the legacy package tests' fixed directory names
from colliding with the parent integration run. The root build registers
`feature_task(M01.Readiness tests/models/ModelReadinessTests.cpp)`; added runtime
cases remain in the existing `M02.SamRuntime` and `M05.Runtime` source targets.

## Focused review and deferred checks

A separate read-only model reviewer inspected the actual source/test diff and
found no CPU correctness blocker. The parent whole-branch review still owns
UI/worker receipt coupling and release acceptance.

The reviewer identified a CUDA-only limitation: adding CUDA to ONNX Runtime does
not disable its default CPU fallback for unsupported graph nodes. The pinned
`onnxruntime_session_options_config_keys.h` lines 267-280 document
`session.disable_cpu_ep_fallback` and its default value `0`. No new fallback guard
was added in this round, per the parent's CPU-only scope decision. Actual CUDA
execution is **Not Tested**, and generic CUDA Ready must not be claimed to prove
exclusive GPU execution. Existing tests cover an unavailable CUDA provider on this
CPU runtime only. This does not undermine an explicitly CPU preflight result.

The following remain **Not Tested / not established by these runs**:

- Windows 11, MSVC, Qt, worker packaging, or native UI behavior.
- Real SAM encoder/decoder weight compatibility or real image segmentation quality.
- The coffee-image integrated acceptance path, reconstruction/export thresholds,
  or any Windows screenshot/artifact requirement.
- Actual CUDA execution, CUDA initialization failures, or GPU-only node placement.

No Windows or real SAM success claim is implied by the synthetic fixture test names.
