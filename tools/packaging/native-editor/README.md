# Deploy the native editor

Use the `native-editor-host-…` artifact from the Linux headless workflow. It includes the compiled x86-64 engine, FastAPI editor, guided segmentation, and the Compose files. The target is a persistent x86-64 Linux machine with Docker Engine and Compose v2. The host downloads Ubuntu packages and Python wheels at image build time. C++ compilation runs in CI.

## Start the owner workspace

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
