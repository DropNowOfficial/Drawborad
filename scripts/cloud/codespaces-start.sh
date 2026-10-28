#!/usr/bin/env bash
# GitHub Codespaces: start a private, bearer-protected Drawborad browser studio.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
DATA="${DRAWBORAD_CLOUD_DATA:-$ROOT/.cloud-data}"
PORT="${DRAWBORAD_CLOUD_PORT:-8765}"
if ! [[ "$PORT" =~ ^[0-9]{1,5}$ ]] || (( PORT < 1 || PORT > 65535 )); then
  echo "Invalid DRAWBORAD_CLOUD_PORT" >&2
  exit 2
fi
mkdir -p "$DATA"
chmod 700 "$DATA"
TOKEN_FILE="$DATA/cloud-token"
if [[ ! -s "$TOKEN_FILE" ]]; then
  python - "$TOKEN_FILE" <<'PY'
import secrets, pathlib, sys
path=pathlib.Path(sys.argv[1])
path.write_text(secrets.token_urlsafe(48))
path.chmod(0o600)
PY
fi
export DRAWBORAD_CLOUD_DATA="$DATA"
export DRAWBORAD_CLOUD_TOKEN
DRAWBORAD_CLOUD_TOKEN="$(cat "$TOKEN_FILE")"
if [[ -f "$DATA/server.pid" ]]; then
  EXISTING="$(cat "$DATA/server.pid")"
  if kill -0 "$EXISTING" 2>/dev/null; then
    echo "Drawborad Cloud Studio already running (PID $EXISTING)."
    echo "Private forwarded port: $PORT; token at $TOKEN_FILE"
    exit 0
  fi
fi
nohup python -m cloud.cli serve --host 0.0.0.0 --port "$PORT" \
  > "$DATA/server.log" 2>&1 < /dev/null &
PID=$!
echo "$PID" > "$DATA/server.pid"
for i in $(seq 1 40); do
  if ! kill -0 "$PID" 2>/dev/null; then
    cat "$DATA/server.log" >&2
    exit 1
  fi
  if python - "$PORT" <<'PY' > /dev/null 2>&1
import sys, urllib.request
urllib.request.urlopen('http://127.0.0.1:'+sys.argv[1]+'/healthz',timeout=0.5)
PY
  then
    echo "Drawborad Cloud Studio running on private forwarded port $PORT (PID $PID)."
    echo "Read access token locally with: cat '$TOKEN_FILE'"
    exit 0
  fi
  sleep 0.2
done
cat "$DATA/server.log" >&2
exit 1