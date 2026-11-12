#pragma once
#include "CoverageRasterizer.h"
namespace drawborad {
// Opt-in engine 10 only. Continuous round segment coverage; never used to replay
// engine 1/2 strokes. Width below one pixel remains partially covered, not opaque.
Result<std::vector<std::uint16_t>> fineLineCoverage(const StrokeEvent&,Rect,
    std::uint64_t timeTicks,std::stop_token={});
}
