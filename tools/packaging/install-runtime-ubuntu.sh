#!/usr/bin/env bash
# Compatibility entry point. New packages use install-runtime-linux.sh.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$here/install-runtime-linux.sh" "$@"
