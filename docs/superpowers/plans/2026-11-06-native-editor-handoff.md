# Native stroke editor: runnable handoff

## Run the actual application

Build on Ubuntu 24.04 with the existing Linux headless instructions, then:

```bash
python3 -m venv .venv
.venv/bin/pip install -r cloud/requirements.txt opencv-python-headless
export DRAWBORAD_NATIVE_ENGINE="$PWD/out/linux-headless/DrawboradHeadless"
export DRAWBORAD_CLOUD_DATA="$PWD/.cloud-data"
.venv/bin/uvicorn cloud.app:app --host 127.0.0.1 --port 8000 --workers 1
```

Use the existing access-token configuration before any remote exposure. This change does not deploy publicly. One owner process is enforced by a workspace file lock; do not use multiple Uvicorn workers.

Open a .draw file, select a stable stroke ID (the last event is selected initially), choose sRGB color, apply, undo/redo, save, download. Painting preview, layer visibility/locking, replay and PNG/ORA exports all call the native session. Earlier strokes can be covered by later strokes; use replay to inspect them.

Expand photo segmentation to upload a photo plus an explicit seed PNG. Review the resulting masks, then accept the reviewed masks and generate native strokes. Seed values are 0/85/170/255. This is human-guided two-region GrabCut, not SAM or automatic multi-object recognition. Existing external manifests and SAM jobs remain available. Real SAM still requires the actual model/runtime.

## Persistence contract

- C++ ApplicationController owns one Document and its undo/redo history.
- HTTP responses are projections of the native snapshot. IDs/revisions/counts are decimal strings or full hexadecimal IDs, never lossy numeric uint64.
- Browser localStorage contains only project ID. Refresh reconnects to the live session.
- Save is explicit. Download refuses dirty state. Server/engine restart reopens the last successfully saved project; unsaved edits and undo history are lost.
- Switching dirty projects is refused. Explicit close can discard. Invalid replacement requests preserve the active document and history.
- IPC is private stdin/stdout JSONL; stderr is separately logged. Inputs are capped before writing; deadlines cover both writes and reads. A timed-out/crashed engine is terminated, and the response explicitly reports that unsaved state is lost.

## Evidence and limitations

The native session contract, API integration tests and independent real-photo HTTP acceptance are executable in CI. Acceptance outputs include before/after PNG, edited .draw, zero/middle/final replay PNG, ORA, server logs and JSON receipt. Independent ORA compositing is compared with native PNG.

Cloud browser access to terminal.local:4173 was explicitly denied by browser URL security policy. No alternative browser surface was used to bypass it. The page's controls are implemented, but actual browser acceptance and visual QA remain BLOCKED, not passed. There is no verified remote application deployment. The milestone is therefore not yet fully accepted.

One independent review found replacement/history loss, oversized IPC causing session loss, and failed checkbox edits remaining displayed. Narrow fixes preserve the controller on failed replacement, bound IPC before writing, and reload native state after rejected UI operations. Regression tests cover the server-side cases; UI behavior still requires the blocked browser gate.

## Resume

1. Build/run this commit and resolve the authorized cloud-browser preview access.
2. Execute real UI open/recolor/undo/redo/save/reload/replay/download and locked/stale rejection checks.
3. Capture actual screenshots and inspect them; do not substitute HTTP receipts for this gate.
