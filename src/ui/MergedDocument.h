#pragma once
#include "render/ReferenceRenderer.h"
namespace drawborad {
// Incremental render allocation/work model, excluding the caller-owned snapshot.
// estimatedBytes is conservative accounting, not measured allocator/RSS usage.
struct MergedBudget {
    std::uint64_t maxBytes=128*1024*1024, maxIndexEntries=65536;
    std::uint64_t maxAxis=8192,maxPixels=20000000;
    std::uint64_t maxCoveragePixels=256*1024*1024, maxGeometryPoints=1000000, maxRasterWork=512*1024*1024;
};
struct MergedPlan {
    std::uint64_t mergedBytes{}, tileScratchBytes{}, indexEntries{}, estimatedBytes{}, coveragePixels{};
};
struct MergedDocument { TileData merged; RenderStatistics statistics; MergedPlan plan; };
Result<MergedPlan> planMergedDocument(const DocumentSnapshot&,MergedBudget={});
Result<MergedDocument> renderMergedDocument(const DocumentSnapshot&,EventLimit,MergedBudget={},std::stop_token={});
}
