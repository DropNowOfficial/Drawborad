#!/usr/bin/env bash
# Opt-in standalone research helper. Does not modify or enable the application.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source=${DRAWBORAD_SOURCE:-"$here/../../.."}
if (( $# > 1 )); then printf 'usage: %s [OUTPUT_BINARY]\n' "$0" >&2; exit 2; fi
output=${1:-"$here/build/native-locality"}
mkdir -p "$(dirname "$output")"
flags=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off -I"$source/src")
modules=(core/DocumentSpec core/Document core/CommandBus core/Digest render/ReferenceRenderer render/CoverageRasterizer render/PixelBlend storage/ProjectStore storage/ProjectCodec storage/TileStore storage/Journal storage/FileIO)
files=(); for module in "${modules[@]}"; do files+=("$source/src/$module.cpp"); done
c++ "${flags[@]}" "$here/NativeLocality.cpp" "${files[@]}" -o "$output"
{
  c++ --version | sed -n '1p'
  printf '%s\n' 'flags: -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off'
  sha256sum "$here/NativeLocality.cpp" "$here/build.sh" "$output" "${files[@]}" "$source/src/view/LayerTree.h"
} > "$output.provenance.txt"
printf 'Built %s\n' "$output"
