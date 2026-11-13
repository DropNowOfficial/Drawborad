#pragma once
#include "QtSolidMaskCache.h"
#include "core/PixelFormat.h"
#include <span>
#include <stop_token>
namespace drawborad {
struct QtSolidDraftPaint {
    std::uint64_t maskId{};
    ColorValue color;
    double opacity{1};
};
// Temporary draft only. Encoded/document-channel semantics match blendCoverage;
// U16 arithmetic is NOT a linear-light color-space conversion. Normal paint only.
// Requires 1..8 distinct masks. Empty paint lists are rejected.
// Global region is <=512 per axis; background has its local region dimensions.
// Input/cache single-thread ownership must remain stable throughout the call.
Result<TileData> renderQtSolidDraftPreview(const QtSolidMaskCache& cache,
    std::uint64_t generation, Rect region, std::span<const QtSolidDraftPaint> paints,
    const TileData& background, std::stop_token stop = {});
}
