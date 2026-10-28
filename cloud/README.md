# Drawborad Cloud Studio

A self-hostable **agent-neutral** browser application and HTTP API for evaluating SAM 3 semantic image segmentation as independent object layers. Runs on cloud Linux/containers; Windows clients need only a browser. CPU-only mode supports import, inspection and OpenRaster export; real SAM 3 inference requires separately approved official checkpoint, official Python module and sufficient GPU. It does **not** claim to run the existing native Drawborad C++ paint engine.

## Fast local verification (no model required)

```sh
python -m pip install -r cloud/requirements.txt pytest httpx
python -m pytest -q tests/cloud
python -m cloud.cli serve                 # http://127.0.0.1:8765
# then click "Open synthetic sample"
```

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