#!/usr/bin/env bash
# Run as root on Ubuntu 24.04; installs runtime libraries, not build tools.
set -euo pipefail
. /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 ]]; then
  echo 'This package supports Ubuntu 24.04 x86_64.' >&2
  exit 2
fi
if [[ "$(uname -m)" != x86_64 ]]; then exit 2; fi
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  libqt6core6t64 libqt6gui6 libpng16-16t64 libjpeg-turbo8 libtiff6 \
  libwebp7 libheif1 liblcms2-2 libstdc++6 zlib1g python3 python3-pil
