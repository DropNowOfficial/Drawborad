#pragma once
#include "FloodFill.h"
#include "core/Commands.h"
namespace drawborad {
Result<Command> bucketFillCommand(const DocumentSnapshot&, NodeId layer, Id event,
    const TileData& reference, std::uint32_t x, std::uint32_t y,
    std::uint8_t tolerance, ColorValue color, std::stop_token stop = {});
}
