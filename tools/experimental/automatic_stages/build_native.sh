#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
output=${1:-"$root/local-build/automatic-stages-native"}
mkdir -p "$(dirname "$output")"
cd "$root"
"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -Wpedantic -Werror \
  -fno-fast-math -ffp-contract=off -Isrc \
  tools/experimental/automatic_stages/NativePlan.cpp \
  src/core/DocumentSpec.cpp src/core/Document.cpp src/core/CommandBus.cpp src/core/Digest.cpp \
  src/render/ReferenceRenderer.cpp src/render/CoverageRasterizer.cpp src/render/FineLineCoverage.cpp src/render/DryInkCoverage.cpp src/render/PixelBlend.cpp \
  src/reconstruct/ReconstructionService.cpp src/reconstruct/EventWriter.cpp \
  src/storage/ProjectStore.cpp src/storage/ProjectCodec.cpp src/storage/TileStore.cpp \
  src/storage/Journal.cpp src/storage/FileIO.cpp -o "$output"
printf 'Built prototype runner: %s\n' "$output"
