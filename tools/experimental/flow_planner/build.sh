#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source=${DRAWBORAD_SOURCE:-"$(cd "$here/../../.." && pwd)"}
mkdir -p "$here/build"
flags=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off -I"$source/src")
c++ "${flags[@]}" -shared -fPIC -Wl,--no-undefined "$here/NativeFeedback.cpp" "$source/src/render/CoverageRasterizer.cpp" "$source/src/render/FineLineCoverage.cpp" "$source/src/render/PixelBlend.cpp" "$source/src/core/Digest.cpp" "$source/src/core/DocumentSpec.cpp" -o "$here/build/libflowfeedback.so"
modules=(core/DocumentSpec core/Document core/CommandBus core/Digest render/ReferenceRenderer render/CoverageRasterizer render/FineLineCoverage render/PixelBlend reconstruct/ReconstructionService reconstruct/EventWriter storage/ProjectStore storage/ProjectCodec storage/TileStore storage/Journal storage/FileIO)
files=(); for module in "${modules[@]}"; do files+=("$source/src/$module.cpp"); done
c++ "${flags[@]}" "$here/NativePlan.cpp" "${files[@]}" -o "$here/build/native-flow"
