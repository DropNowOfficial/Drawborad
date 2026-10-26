# RC1 validation input provenance

These inputs are for this Linux engineering session; they are not a Windows
deployment or actual SAM 2.1 weight package. External inputs are not committed.

| Input | Origin | Verification / use |
|---|---|---|
| Repository | https://github.com/DropNowOfficial/Drawborad, `impl/full-scene-checkpoint-20261025` | Remote HEAD `69504ea037bf3a30c7a7afae2f19dc8742791610`; all 299 blobs and tree `2027d1ceea8a62b4b42abd20ae6f1bb453caa436` matched |
| Coffee image | https://raw.githubusercontent.com/scikit-image/scikit-image/v0.25.2/skimage/data/coffee.png | 600 x 400; SHA-256 `cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7`; recovered original input, not synthesized |
| Linux CPU ONNX Runtime 1.23.2 SDK | https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-linux-x64-1.23.2.tgz | Archive SHA-256 `1fa4dcaef22f6f7d5cd81b28c2800414350c10116f5fdd46a2160082551c5f9b`; upstream LICENSE/ThirdPartyNotices retained with SDK |
| Tiny ONNX interface fixtures | Existing `.github/workflows/cm-ort-runtime.yml` generator in this source baseline | Identity, encoder/decoder and adapter probe graphs; synthetic contract tests only, never described as SAM weights or real segmentation |
| SAM 2.1 local weight package | Not available in this session | Blocked; no silent weight download or redistribution |
| Official coffee tensor / automatic / full-scene references | Not available in this session | Blocked; existing reviewed masks alone cannot substitute for these inputs |

Session paths: `/tmp/drawborad-rc1-deps/coffee.png`,
`/tmp/drawborad-rc1-deps/onnxruntime-linux-x64-1.23.2`, and
`out/rc1-runtime-fixtures`. They are temporary local test dependencies. Hashes
are rechecked by the configure/test inventory when selected. Model readiness
retains package provenance and checks manifest/encoder/decoder digests; neither
a file's existence nor a CPU runtime capability is itself a Ready receipt.

The official coffee URL provides the original rather than a generated visual.
No new source image, ONNX weight, or third-party SDK has been uploaded to GitHub.

Portable codec checks used isolated official Ubuntu 24.04 development packages:
PNG 1.6.43-5ubuntu0.7 (matching private headers/runtime), JPEG turbo 2.1.5-2ubuntu2,
TIFF 4.5.1+git230720-4ubuntu2.5, WebP 1.3.2-0.4build3 and LCMS 2.14-2ubuntu0.1.
All six downloaded package hashes were checked against signed Ubuntu indexes;
the remaining matching host runtime libraries and zlib were reused. Exact URLs,
package/runtime hashes, signature checks, init-cache commands and distribution
copyright files are retained in `out/rc1-evidence/codec-dependencies`.
