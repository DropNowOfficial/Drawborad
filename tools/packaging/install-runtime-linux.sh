#!/usr/bin/env bash
# Install distribution-native runtime libraries, never cross-distribution ABI aliases.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if (( $# > 1 )) || [[ $# == 1 && $1 != --print-packages ]]; then
  echo 'Usage: bash install-runtime-linux.sh [--print-packages]' >&2
  exit 2
fi
if ! command -v python3 >/dev/null; then
  echo 'Python 3 is required. Bootstrap it from this distribution before running this installer.' >&2
  exit 2
fi
# Capture first: mapfile with process substitution would hide a selector failure.
selected="$(python3 "$here/runtime_platform.py" packages)"
mapfile -t packages <<< "$selected"
if [[ "${1:-}" == --print-packages ]]; then
  printf '%s\n' "${packages[@]}"
  exit 0
fi
if (( EUID != 0 )); then
  echo 'System package installation requires root; use sudo only when authorized.' >&2
  exit 2
fi
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
