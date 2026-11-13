#pragma once
#include "CoverageRasterizer.h"
namespace drawborad {
Result<std::vector<std::uint16_t>> chiselInkCoverage(const StrokeEvent&,Rect,std::uint64_t,std::stop_token={});
}
