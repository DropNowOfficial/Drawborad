# One image, two finite jobs

This branch proves the **explicit fixture → persisted review → selected native objects → .draw/PNG/ORA** contract. It does not claim a real SAM3 GPU run, cloud-storage qualification, actual cloud billing or ARES_207 accuracy.

## Run the available contract

Use the built DrawboradHeadless from this branch (baseline binary lacks --accept-index). Install cloud/requirements.txt and httpx. Set DRAWBORAD_NATIVE_ENGINE to its absolute path, DRAWBORAD_CLOUD_TOKEN to a private owner token, DRAWBORAD_BATCH_DATA to persistent storage, and DRAWBORAD_BATCH_FIXTURE=1. Start:

```
python -m uvicorn cloud.batch.runtime:make_app --factory --host 127.0.0.1 --port 8766
```

Open that address, connect with the token, upload an image, explicitly select fixture mode, run, inspect candidates, tick accepted objects, generate, download. The fixture divides visible pixels into two vertical regions; it is not an object detector. Inputs keep original dimensions; supported formats/size/alpha match the manifest contract. The gateway starts separate short-lived Python processes for development tests. No GPU is used. Keep this development gateway private.

Run independent HTTP acceptance with `python tests/batch/acceptance.py /absolute/path/DrawboradHeadless photo.png evidence`. Add `--browser` where Chromium/Playwright is installed. Production files stay in private storage; local worker directories are destroyed. Existing native editor remains available using its unchanged launcher; import the downloaded .draw there.

## Production boundary and deployment assets

The gateway is stateless with `DRAWBORAD_BATCH_STORE=s3`. Configure private `DRAWBORAD_S3_ENDPOINT` (HTTPS), `DRAWBORAD_S3_BUCKET`, `DRAWBORAD_S3_PREFIX`, AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY server-side. Use bucket-scoped least-privilege credentials. S3Store requires atomic If-Match/If-None-Match; qualify the actual provider before use. The directory store is for development only and does not survive loss of its disk. Gateway/API authentication is single owner, not multi-tenant isolation. Files are authenticated gateway downloads, not public bucket objects.

`Dockerfile.cpu` builds the C++ engine and can execute `python -m cloud.batch.worker --job-id ID --phase build` in a finite CPU container. The same image may serve the stateless gateway on an on-demand HTTP platform. **No CPU provider is wired by default.** RunPodDispatcher accepts a cpu_dispatch callable; production build returns an explicit configuration error until one is connected. It does not silently spend GPU time for CPU reconstruction.

`Dockerfile.gpu` installs official SAM3 code at an explicitly supplied SAM3_REF commit. It never contains weights. Privately provision a licensed official **image** checkpoint through the platform, and set SAM3_CHECKPOINT / SAM3_CHECKPOINT_SHA256. Model download/access approval is deliberately not automatic. Worker refuses absent CUDA/checkpoint or no detections. It writes durable manifest/masks/previews before returning. No worker waits for review.

RunPod initial endpoint settings to verify in the actual console/API: Flex, active/min workers 0, max workers 1, GPUs 1, idle timeout 5s, execution timeout 180s, TTL 600s, 24GB GPU first. Confirm platform retry behavior and billing before enabling. The code does not create/buy an endpoint. Set RUNPOD_ENDPOINT_ID, RUNPOD_API_KEY and explicit DRAWBORAD_TRIAL_APPROVED_USD (0 by default, max 10) only after preflight. One global trial reservation prevents another paid submission until an operator reconciles the provider's usage. Lost /run replies remain `submission_unknown` and must never be automatically retried. This is a conservative first-trial lock, not a provider-enforced dollar spending cap. Provider retries/startup can still bill; verify limits externally.

Sites can host the frontend/gateway but cannot run these native containers. Existing published Sites is unchanged. Connecting production requires an authorized serverless compute account, private object store and approved model access. No standing CPU VPS is required by the contract.

## Cost preflight and reconciliation (required before any paid call)

Official references checked 2026-11-11:
- https://docs.runpod.io/serverless/pricing
- https://docs.runpod.io/serverless/workers/overview
- https://docs.runpod.io/serverless/endpoints/send-requests
- https://github.com/facebookresearch/sam3

Get a current **Flex** 24GB quote; do not substitute a Pod hourly price. Record currency, GPU class, rate/second, quote timestamp, startup allowance, execution bound, idle timeout, retry bound, disk/storage/egress estimates. Estimate = quoted rate × (billed startup + bounded execution + idle + retries) + storage/network. No verified account-specific quote is available in this session, so paid dispatch is not approved/configured. Review totals against $10 before a first call.

After execution record provider job/worker IDs, health snapshots showing no Running workers, actual start/stop billing intervals, account usage delta, model loading/inference timings, peak VRAM/RSS, output sizes and storage/network receipt. Handler duration alone is NOT total billable time. Failed and cancelled work can bill. `actual_usd`, billed seconds and cold-start metrics remain null until verified; a successful fixture costs no GPU invocation but is not a production cost benchmark. Idle after scale-down is unbilled compute; the warm idle timeout and persistent storage can still cost money. Never promise literal zero total idle infrastructure cost.

## Complex views / ARES_207

Confirmed non-overlapping view boxes may be supplied with distinct IDs. SAM3 runs inside each view and maps masks to original coordinates, keeping names such as front/helmet and detail/helmet separate. This is **user-confirmed regional inference**, not automatic layout detection. Unrecognized pixels stay explicit background; occluded pixels are not invented. No ARES_207 input or real model acceptance is claimed. Automatic layout detection, reliable part prompts/accuracy, and full-resolution quality remain separate unverified work.

## Recovery limitations

Cancellation wins publication races. Provider cancellation is requested separately; a lost submission response needs console reconciliation. A local development child is bounded by execution timeout and may finish after cancellation, but cannot publish a completed job. Reading status expires a crashed running worker after its deadline; provider billing still requires explicit reconciliation; no paid automatic retry. Browser refresh reconnects using job ID only; tokens are not persisted. Existing persistent editing undo history is not moved into batch workers.
