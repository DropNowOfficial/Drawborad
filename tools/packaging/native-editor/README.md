# Deploy the native editor

Use the `native-editor-host-…` artifact from the Linux headless workflow. It includes the precompiled Ubuntu 24.04 x86-64 engine, FastAPI editor, guided segmentation, a foreground session launcher, and Compose files.

## Use the editor on a Dot's cloud computer

Download the artifact for the push to the current PR branch commit through the connected GitHub account. Unzip it into a writable directory on the cloud computer. Then run:

```sh
sha256sum -c DrawboradNativeEditor-ubuntu24-amd64.tar.gz.sha256
tar xzf DrawboradNativeEditor-ubuntu24-amd64.tar.gz
bash start-cloud-session.sh
```

Run this in a terminal that can stay open. Once the server is ready, open **http://127.0.0.1:8767** in the browser on that same computer, or use its approved private port preview. Enter the owner token from the file path printed in the terminal. Keep the token in the cloud computer and out of chat or screenshots.

The first start sets up a private Python environment and downloads wheels. If the launcher reports missing system libraries, `bash start-cloud-session.sh --install-deps` can install the packaged Ubuntu dependencies where noninteractive administrator access is allowed. The editor itself runs as the calling user. It binds to loopback and stays available while its process runs.

Saved drawings live under `${XDG_DATA_HOME:-$HOME/.local/share}/drawborad-native-session/data/projects`. Use **Save project** and download the `.draw` if the cloud workspace is temporary. Stop the server with Ctrl+C. Running the same command again reuses the owner token and saved projects. Set `DRAWBORAD_SESSION_HOME` for a different persistent directory or `DRAWBORAD_CLOUD_PORT` for a different loopback port.

## Run a persistent host with Docker Compose

Unzip the GitHub artifact in a new directory on the host, then run:

```sh
sha256sum -c DrawboradNativeEditor-ubuntu24-amd64.tar.gz.sha256
tar xzf DrawboradNativeEditor-ubuntu24-amd64.tar.gz
config_dir="$HOME/.config/drawborad-native"
mkdir -p -m 700 "$config_dir"
token_file="$config_dir/owner.env"
# Create once. Reusing the same file preserves the owner's login on upgrade.
if [ ! -e "$token_file" ]; then
  (umask 077; set -C; python3 -c 'import secrets; print("DRAWBORAD_CLOUD_TOKEN=" + secrets.token_urlsafe(32))' > "$token_file")
fi
docker compose --env-file "$token_file" up -d --build --wait
curl --fail http://127.0.0.1:8767/healthz
```

Use the token value from the owner's private file in the editor's **Access token** field. The browser asks again after refresh. The project ID survives refresh and the running engine retains the current edit history. **Save project** writes the painting to the `paintings` volume. Recreating the container restores the last save. Keep the Compose project name and volume when upgrading, and back up that volume.

## Connect the published Site

Configure an HTTPS reverse proxy on the authorized host or an existing authenticated tunnel to `http://127.0.0.1:8767`. Preserve paths, query strings and the caller's `Authorization` header. Allow time for native reconstruction and use a body limit sufficient for the files you intend to import. Direct HTTP access to the container is bound to host loopback.

1. Check that an anonymous `GET /api/v1/capabilities` at the **HTTPS origin** returns 401. With the owner token, verify 200 and `native_engine_configured: true`.
2. Set **only** `DRAWBORAD_BACKEND_ORIGIN` to that HTTPS origin in the existing Site. Each visitor supplies their own owner credential for protected operations. Keep the backend token out of the public proxy, build output and repository.
3. Open the public editor, authenticate, edit a stroke, undo, redo, save, refresh and reconnect. Verify the .draw, PNG and ORA downloads on that public route. CI evidence covers the packaged editor on a disposable Linux host.
