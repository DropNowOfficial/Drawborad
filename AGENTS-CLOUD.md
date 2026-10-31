# Drawborad Cloud Studio: instructions for any connected coding agent

Target: build and verify a **Linux-hosted drawing application** usable by browsers and platform-neutral agents. Windows adaptation comes later.

Read the [Linux cloud architecture](docs/superpowers/specs/2026-10-29-linux-cloud-architecture-design.md) first. It distinguishes current implementation from proposed work and supersedes the Windows-first readiness proposal. The first implementation slice is Linux headless reconstruction/save/replay/export, not desktop brush wiring.

1. Check out the repository's `work/cloud-agent-runtime-20261028` branch. Do not modify or merge the Windows RC1 branch.
2. Prefer a GitHub Codespace on that branch for browser-based cloud testing without local Linux. It auto-runs `scripts/cloud/codespaces-start.sh`, binds port 8765 privately, and places its access token in `.cloud-data/cloud-token`. Otherwise, install `python -m pip install -r cloud/requirements.txt pytest httpx` and run `python -m pytest -q tests/cloud`.
3. Run `python -m cloud.cli smoke --output /tmp/drawborad-cloud-smoke` and inspect `.ora` as an actual ZIP with `mimetype=image/openraster` and `stack.xml`.
4. Run `python -m cloud.cli serve --host 127.0.0.1 --port 8765`; use the browser at `http://127.0.0.1:8765` or call REST documented at `/docs`. For a Docker deployment, follow `docs/cloud/CLOUD_AGENT_RUNBOOK.md`.
5. Run a real SAM 3 image inference **only** if licensed official checkpoint, official `sam3` Python module and a capable GPU are accessible. Set the relevant environment variables; never silently substitute a synthetic test or GrabCut result.
6. Evaluate four independent layers (wood, saucer, cup, spoon), visible accuracy, OpenRaster export; report both achieved and failed steps and paths of created files.

The SAM3 **image** checkpoint must not be confused with SAM3.1 multiplex **video** checkpoints. Synthetic demo is labeled `fixture-no-ai`. The current cloud cutout workspace is not proof that Linux headless painting, editable projects or source-free replay are integrated. Report those separately from semantic segmentation.