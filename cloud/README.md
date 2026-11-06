# Drawborad Cloud Studio

Drawborad Cloud Studio serves the browser UI and HTTP API. The review-only image supports layer inspection and OpenRaster export. The **native editor host bundle** adds the persistent C++ engine for editing, replaying and saving real brush strokes. Licensed SAM 3 inference requires its separate model and runtime.

For a persistent Linux host, download the `native-editor-host-…` artifact from the Linux headless workflow and follow the [native editor deployment instructions](../tools/packaging/native-editor/README.md). It includes the precompiled engine, a loopback-bound Compose service and persistent project storage.

## Fast local verification (no model required)

```sh
python -m pip install -r cloud/requirements.txt pytest httpx
python -m pytest -q tests/cloud
python -m cloud.cli serve                 # http://127.0.0.1:8765
# then click "Open synthetic sample"
```

## GitHub Codespaces — no local Linux, Docker or Python installation

On the GitHub branch `work/cloud-agent-runtime-20261028`: **Code → Codespaces → Create codespace**. After it starts, open the private forwarded port 8765, get the password by running `cat .cloud-data/cloud-token` in its terminal, and paste it into the Cloud Studio browser UI. See the runbook for details. This validates the CPU review UI, not GPU segmentation accuracy.

## Cloud / Docker

See [full runbook](../docs/cloud/CLOUD_AGENT_RUNBOOK.md) and [`AGENTS-CLOUD.md`](../AGENTS-CLOUD.md).

```sh
export DRAWBORAD_CLOUD_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
docker compose -f compose.cloud.yaml up --build -d review
```

For a real official SAM3 GPU installation, obtain the license-gated image checkpoint and run the `sam3-gpu` Compose profile. See runbook for details. Weights are never redistributed by Drawborad.

## Agent API

OpenAPI: `http://127.0.0.1:8765/docs`.

`GET /healthz`, `GET /api/v1/capabilities`, `POST /api/v1/jobs` with image + prompts, `GET /api/v1/jobs/{id}`, `GET /api/v1/jobs/{id}/layers/{index}.png`, `GET /api/v1/jobs/{id}/layers.ora`, `POST /api/v1/import` with model manifest ZIP, `POST /api/v1/demo` synthetic fixture, and `DELETE /api/v1/jobs/{id}` cancel.

`GET /healthz` only reports HTTP service health, **not** model correctness. Imported external manifests are labeled `origin=external-import`, and do not claim inference happened on this host.
