#pragma once
#include "ReferenceRenderer.h"
namespace drawborad {
Result<std::vector<std::uint16_t>> strokeCoverage(const StrokeEvent&, Rect,
                                                  std::uint64_t timeTicks,
                                                  std::stop_token = {});
void blendCoverage(TileData&, const StrokeEvent&, std::span<const std::uint16_t>);
Result<void> validateRasterEvent(const StrokeEvent&);
}
