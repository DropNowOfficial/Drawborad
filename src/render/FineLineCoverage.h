#pragma once
#include "CoverageRasterizer.h"
namespace drawborad {
// Opt-in engines 10 (fine) and 19 (broad solid ink). Continuous round segment
// coverage; never used to replay engine 1/2 strokes. Subpixel width stays partial.
Result<std::vector<std::uint16_t>> fineLineCoverage(const StrokeEvent&,Rect,
    std::uint64_t timeTicks,std::stop_token={});
}
