# Deploy the native editor

Use the distro-qualified `native-editor-host-<target>-<commit>` artifact from the Linux headless workflow. It includes the native engine, FastAPI editor, guided segmentation, a foreground session launcher, and Compose files.

## Choose the matching runtime

These are two native builds, not one binary with renamed shared libraries:

| Cloud host | Artifact target | JPEG runtime |
| --- | --- | --- |
| Ubuntu 24.04, x86-64 | `ubuntu24.04-x86_64` | `libjpeg.so.8` (`libjpeg-turbo8`) |
| Debian 13, x86-64 | `debian13-x86_64` | `libjpeg.so.62` (`libjpeg62-turbo`) |

Read `/etc/os-release` and `uname -m` on the **cloud host**, not the browser client. Each package contains `runtime-target.json` with OS, version, architecture and source commit. The launcher checks it before executing the engine or creating session files. Do not use an Ubuntu binary on Debian, copy Ubuntu system packages into Debian, or symlink `libjpeg.so.8` to `libjpeg.so.62`. Qt and glibc are also built and tested against the matching distribution.

Other distributions, releases and CPU architectures are not qualified. The Windows desktop product is unchanged. This support does not install a second OS on the user's computer.

The workflow must pass for **both** targets before a new build is called qualified. Adding a matrix or passing the packaging unit tests is not proof that the native engine or browser acceptance ran.

## Use the editor on a Dot's cloud computer

Download the artifact for the push to the current PR branch commit through the connected GitHub account. Unzip it into a writable directory on the cloud computer. Then run:

```sh
# Debian 13. For Ubuntu 24.04, use target=ubuntu24.04-x86_64.
target=debian13-x86_64
sha256sum -c "DrawboradNativeEditor-$target.tar.gz.sha256"
tar xzf "DrawboradNativeEditor-$target.tar.gz"
# Check native libraries, Python venv, flock and storage without writes or tokens.
bash start-cloud-session.sh --check
# Only start after approving local owner-credential creation if needed.
bash start-cloud-session.sh
```

Run this in a terminal that can stay open. Once the server is ready, open **http://127.0.0.1:8767** in the browser on that same computer, or use its approved private port preview. Enter the owner token from the file path printed in the terminal. Keep the token in the cloud computer and out of chat or screenshots.

The first start sets up a private Python environment and downloads wheels. Python 3 must already be present to run the platform check. On a minimal authorized host, bootstrap Python 3 from that distribution's official repositories. If the launcher reports missing system libraries, `bash start-cloud-session.sh --install-deps` installs the matching distribution dependencies where noninteractive administrator access is allowed. It never installs Ubuntu libraries on Debian. `bash tools/packaging/install-runtime-linux.sh --print-packages` lists dependencies without installing anything. The editor itself runs as the calling user. It binds to loopback and stays available while its process runs.

`--check` does not install packages, create a virtual environment, create a token, or start a server. It verifies only the native/Python prerequisites and the nearest existing session-storage parent; it does not claim the Python application dependencies are installed or guarantee disk capacity. Actual startup can still fail due to package/network/storage limits. A read-only filesystem needs an authorized writable persistent directory; do not bypass its mount restrictions.

The default first start creates a persistent owner credential. On a shared or managed host, obtain the owner's approval before creating it. Do not disable authentication or use a public fixed token to make the preflight pass. Existing credentials and project storage remain unchanged.

Saved drawings live under `${XDG_DATA_HOME:-$HOME/.local/share}/drawborad-native-session/data/projects`. Use **Save project** and download the `.draw` if the cloud workspace is temporary. Stop the server with Ctrl+C. Running the same command again reuses the owner token and saved projects. Set `DRAWBORAD_SESSION_HOME` for a different persistent directory or `DRAWBORAD_CLOUD_PORT` for a different loopback port.

## Run a persistent host with Docker Compose

Unzip the matching GitHub artifact in a new directory on the host. Its Dockerfile uses the same distribution as its binary and Compose requests `linux/amd64`. The container host may use another Linux distribution, but do not override the packaged image to a different release. Then run:

```sh
# Debian 13. For Ubuntu 24.04, use target=ubuntu24.04-x86_64.
target=debian13-x86_64
sha256sum -c "DrawboradNativeEditor-$target.tar.gz.sha256"
tar xzf "DrawboradNativeEditor-$target.tar.gz"
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
