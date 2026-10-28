# Drawborad Cloud Studio | Agent / cloud computer runbook

**Status: testable cloud layer-workspace, NOT production native C++ desktop or proved SAM3 semantic accuracy.**

Designed for users without Linux installed locally. Agents can run this on a Linux cloud VM/container, a GitHub Actions runner or an isolated cloud computer **if that environment grants terminal/Docker access**. Being named Dots, Grok or another agent does not imply that agent can install containers or access your private GitHub repository. Connect the repo or give it the checked-out files, and use the steps below.

## What this actually does

- Upload local PNG/JPEG and run the repository's official SAM 3 **image** prompt adapter with a licensed checkpoint on a CUDA GPU.
- Strictly verify named layer masks: SHA-256, binary coverage, no shared visible pixels, no malformed paths, transparency limits. Refuse invalid results rather than emitting a misleading layer document.
- Inspect layers individually in the browser, hide/show each, and export valid OpenRaster (.ora), each layer's RGBA PNG, and a native-compatible manifest.
- Deterministic artificial sample and ZIP import/export make the cloud/agent workflow fully testable *before* acquiring a GPU.
- This cloud component **does not run Drawborad's C++ paint/stroke reconstruction** yet. That engine still needs its own native integration/real-image qualification.

## Zero-install option: GitHub Codespaces (recommended first test)

From the private Drawborad repository, select branch `work/cloud-agent-runtime-20261028`, then **Code → Codespaces → Create codespace on this branch** (requires access to GitHub Codespaces on the account). The devcontainer installs the cloud Python dependencies and starts the service automatically.

1. Wait until the Codespace terminal shows `Drawborad Cloud Studio running on private forwarded port 8765`.
2. In **Ports**, open forwarded port `8765` (configured **private**, not public).
3. In the Codespaces terminal run `cat .cloud-data/cloud-token` and paste the token into the browser UI access token field.
4. Click **Open synthetic sample**, toggle independent layers and export `.ora`, or import a previously generated SAM3 manifest ZIP.

This is an actual cloud computer running Linux **for you**; your own Windows machine only uses the browser. The synthetic sample is not AI inference. A regular Codespace normally does **not** provide an NVIDIA GPU; use the separate GPU deployment below to test real SAM3 inference.

## A: No GPU needed — run browser and layer tests in any Docker cloud computer

```bash
git clone <YOUR_CONNECTED_PRIVATE_DRAWBORAD_REPOSITORY_URL> drawborad
cd drawborad
export DRAWBORAD_CLOUD_TOKEN="$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')"
docker compose -f compose.cloud.yaml up --build -d review
```

Open `http://127.0.0.1:8765` **inside that cloud computer or through an HTTPS-authenticated tunnel**. Paste `DRAWBORAD_CLOUD_TOKEN` into the UI if the deployment requires it. Click **Open synthetic sample**; enable/disable layers, export `.ora`. You do *not* need Linux on your own Windows PC.

With direct Python on Windows 11/macOS/Linux:

```bash
python -m pip install -r cloud/requirements.txt
python -m cloud.cli serve
# browser: http://127.0.0.1:8765, no token required when bound to loopback
```

## B: Real official SAM 3 GPU inference

1. Acquire access to the official [Meta SAM3 image checkpoint](https://huggingface.co/facebook/sam3). Check license; do not distribute weights publicly. Download `sam3.pt` into a **host-local** folder such as `/workspace/models`.
2. Install Docker, NVIDIA Container Toolkit and an NVIDIA GPU with a compatible driver on the host.
3. Launch the GPU container after setting these variables:

```bash
export SAM3_MODEL_DIR=/workspace/models
export DRAWBORAD_CLOUD_TOKEN="$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')"
docker compose -f compose.cloud.yaml --profile gpu up --build -d sam3-gpu
```

Open `http://127.0.0.1:8766` on the GPU host, or a securely tunneled URL. Run with an actual image and object list such as `wooden table, saucer, coffee cup, spoon`. Cloud GPU images are large and require an available registry, toolchain, network and host GPU; this project's GPU container has **not** been compiled or exercised on an NVIDIA host in the current verification environment.

### Cloud agent CLI / REST API

```bash
python -m cloud.cli smoke --output /tmp/drawborad-smoke
python -m cloud.cli inspect --dir /tmp/drawborad-smoke
python -m cloud.cli export --dir /tmp/drawborad-smoke --ora /tmp/drawborad.ora
python -m cloud.cli segment --image /data/coffee.png --output /data/result --checkpoint /models/sam3.pt --device cuda --prompt 'wooden table' --prompt 'saucer' --prompt 'coffee cup' --prompt 'spoon'
```

REST (replace `$TOKEN`, `$HOST` and input paths):

```bash
curl -H "Authorization: Bearer $TOKEN" "$HOST/api/v1/capabilities"
curl -X POST -H "Authorization: Bearer $TOKEN" "$HOST/api/v1/demo"
curl -X POST -H "Authorization: Bearer $TOKEN" -F "image=@/data/coffee.png" -F "prompts=wooden table, saucer, coffee cup, spoon" "$HOST/api/v1/jobs"
curl -H "Authorization: Bearer $TOKEN" "$HOST/api/v1/jobs/$JOB_ID"
curl -H "Authorization: Bearer $TOKEN" -o layers.ora "$HOST/api/v1/jobs/$JOB_ID/layers.ora"
```

Full live schema: `$HOST/docs`. API jobs persist on a server-owned volume. SAM3 checkpoints and raw user images are never shipped in container layers. Only verified model outputs are marked `backend: sam3-image`; synthetic fixtures are explicitly tagged `fixture-no-ai`.

## GitHub / automation

Open a PR against the RC1 branch: `.github/workflows/cloud-agent-runtime.yml` tests Python + validates an actual built **CPU Docker image** using HTTP + records artifact files. If the private repository's GitHub Actions policy or billing prevents runs, the CI will remain blocked; do not mark success based on YAML presence alone.

## Security and non-goals

- Never expose this prototype as an unauthenticated public internet service. Non-loopback `cloud.cli serve` refuses to boot without `DRAWBORAD_CLOUD_TOKEN`; use HTTPS and an auth proxy for remote users, even if the bearer token is set.
- Per-file input limits: 32 MiB, 4 million pixels; bundled ZIP is bounded and inspected before extraction. Layer overlap or missing coverage are hard errors.
- Host must protect volume backups and model files. Public multi-user SaaS requires persistent auth, per-user namespaces, quotas, TLS, audit logging, antivirus/untrusted media sandboxing and background job orchestration beyond this single-operator development runtime.
- **No hidden-pixel synthesis:** moving an object leaves a transparency hole; alpha layer correctness is not an occlusion-aware reconstruction.
- **No automatic claims about Photoshop fidelity or reconstructed strokes:** OpenRaster layers are real, but the native `.draw` stroke pipeline is still a separately testable integration.