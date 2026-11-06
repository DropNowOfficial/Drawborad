#!/usr/bin/env bash
# Run the extracted Linux editor in the foreground on the owner's cloud computer.
set -euo pipefail
set +x
umask 077

if (( $# > 1 )) || [[ $# == 1 && $1 != --install-deps ]]; then
  echo 'Usage: bash start-cloud-session.sh [--install-deps]' >&2
  exit 2
fi
bundle="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
engine="$bundle/DrawboradHeadless"
if [[ "$(uname -s)/$(uname -m)" != Linux/x86_64 || ! -x "$engine" ]]; then
  echo 'Extract the native-editor-host archive on an x86-64 Linux computer first. The binary targets Ubuntu 24.04.' >&2
  exit 2
fi
port="${DRAWBORAD_CLOUD_PORT:-8767}"
if ! [[ "$port" =~ ^[0-9]{1,5}$ ]] || (( 10#$port < 1 || 10#$port > 65535 )); then
  echo 'DRAWBORAD_CLOUD_PORT must be between 1 and 65535.' >&2
  exit 2
fi

work="${DRAWBORAD_SESSION_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/drawborad-native-session}"
mkdir -p -- "$work"
work="$(cd -- "$work" && pwd -P)"
exec 9>>"$work/session.lock"
if ! flock -n 9; then
  echo "An editor session already owns $work. Reuse its browser or stop it before restarting." >&2
  exit 1
fi
if ! "$engine" --help >/dev/null 2>&1 || ! python3 -c 'import ensurepip, venv' >/dev/null 2>&1; then
  if [[ "${1:-}" != --install-deps ]]; then
    echo 'Missing native libraries or Python venv. On Ubuntu 24.04, rerun with --install-deps if system package installation is allowed.' >&2
    exit 2
  fi
  elevate=()
  if (( EUID != 0 )); then elevate=(sudo -n); fi
  "${elevate[@]}" bash "$bundle/tools/packaging/install-runtime-ubuntu.sh"
  "${elevate[@]}" apt-get install -y --no-install-recommends python3-venv ca-certificates
fi
"$engine" --help >/dev/null

venv="$work/venv"
if [[ ! -x "$venv/bin/pip" ]]; then python3 -m venv "$venv"; fi
"$venv/bin/python" -m pip --disable-pip-version-check install --only-binary=:all: \
  -r "$bundle/cloud/requirements.txt" opencv-python-headless

if [[ ! -v DRAWBORAD_CLOUD_TOKEN ]]; then
  token_file="$work/owner-token"
  if [[ ! -e "$token_file" ]]; then
    tmp="$(mktemp "$work/.owner-token.XXXXXX")"
    "$venv/bin/python" -c 'import secrets; print(secrets.token_urlsafe(32))' > "$tmp"
    mv -- "$tmp" "$token_file"
  fi
  DRAWBORAD_CLOUD_TOKEN="$(< "$token_file")"
  echo "Owner token file: $token_file"
fi
if ! (export LC_ALL=C; [[ "$DRAWBORAD_CLOUD_TOKEN" =~ ^[[:graph:]]+$ ]]); then
  echo 'The owner token must contain printable ASCII characters without whitespace.' >&2
  exit 2
fi
export DRAWBORAD_CLOUD_TOKEN
export DRAWBORAD_CLOUD_DATA="$work/data" DRAWBORAD_NATIVE_ENGINE="$engine" WEB_CONCURRENCY=1
cd -- "$bundle"
echo "Open http://127.0.0.1:$port on this cloud computer when the server is ready."
echo "Saved projects: $DRAWBORAD_CLOUD_DATA/projects"
exec "$venv/bin/python" -m cloud.cli serve --host 127.0.0.1 --port "$port"
