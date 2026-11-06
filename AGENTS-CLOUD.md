# Drawborad Cloud Studio: instructions for any connected coding agent

Target: test the **cloud workspace** without requiring Linux on the user's Windows computer.

1. For the native stroke editor, use [PR #6](https://github.com/DropNowOfficial/Drawborad/pull/6). In the Dot's cloud computer, download the `native-editor-host-…` artifact built for the current branch commit, verify its checksum, extract the tarball, and run `bash start-cloud-session.sh`. Open the private loopback URL in the browser on that same computer. See the [session instructions](tools/packaging/native-editor/README.md). Use the existing Business Site only for a separately configured HTTPS gateway.
2. For the older layer-review workspace, the `work/cloud-agent-runtime-20261028` Codespace starts the review UI on private forwarded port 8765 with `scripts/cloud/codespaces-start.sh`. Its token is in `.cloud-data/cloud-token`. Keep the Windows RC1 branch separate.
3. Run `python -m cloud.cli smoke --output /tmp/drawborad-cloud-smoke` and inspect `.ora` as an actual ZIP with `mimetype=image/openraster` and `stack.xml`.
4. Run `python -m cloud.cli serve --host 127.0.0.1 --port 8765`; use the browser at `http://127.0.0.1:8765` or call REST documented at `/docs`. For a Docker deployment, follow `docs/cloud/CLOUD_AGENT_RUNBOOK.md`.
5. Run a real SAM 3 image inference **only** if licensed official checkpoint, official `sam3` Python module and a capable GPU are accessible. Set the relevant environment variables; never silently substitute a synthetic test or GrabCut result.
6. Evaluate four independent layers (wood, saucer, cup, spoon), visible accuracy, OpenRaster export; report both achieved and failed steps and paths of created files.

The SAM3 **image** checkpoint must not be confused with SAM3.1 multiplex **video** checkpoints. Synthetic demo is labeled `fixture-no-ai`. Cloud workspace is not proof the native Windows Qt/C++ painting engine has passed.
