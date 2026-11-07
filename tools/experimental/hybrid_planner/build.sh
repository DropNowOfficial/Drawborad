#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source=${DRAWBORAD_SOURCE:-"$(cd "$here/../../.." && pwd)"}
flow="$here/../flow_planner"
if [[ ! -f "$flow/flow.py" ]]; then flow="$here/../drawborad-flow-planner-next"; fi
# Rebuild the existing FFI implementation on every explicit build. An old .so
# must not silently survive changes to its source or native dependencies.
DRAWBORAD_SOURCE="$source" bash "$flow/build.sh"
mkdir -p "$here/build"
flags=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off -I"$source/src")
modules=(core/DocumentSpec core/Document core/CommandBus core/Digest render/ReferenceRenderer render/CoverageRasterizer render/PixelBlend reconstruct/ReconstructionService reconstruct/EventWriter storage/ProjectStore storage/ProjectCodec storage/TileStore storage/Journal storage/FileIO)
files=(); for module in "${modules[@]}"; do files+=("$source/src/$module.cpp"); done
c++ "${flags[@]}" "$here/NativePlan.cpp" "${files[@]}" -o "$here/build/native-hybrid"
