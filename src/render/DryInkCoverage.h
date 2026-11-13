#pragma once
#include "CoverageRasterizer.h"
namespace drawborad {
inline constexpr std::uint64_t DryInkNearestBytesPerPixel=32;
Result<std::vector<std::uint16_t>> dryInkCoverage(const StrokeEvent&,Rect,std::uint64_t,std::stop_token = {});
}
