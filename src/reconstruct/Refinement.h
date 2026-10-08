#pragma once
#include "Planning.h"
#include "render/ReferenceRenderer.h"
#include <cstdint>
#include <stop_token>
namespace drawborad {
enum class QualityStatus { Converged, NeedsReview };
struct QualityBudget {
    double colorMaeTarget{0.04};
    double alphaMaeTarget{0.04};
    std::uint64_t maxAdditionalEvents{64};
    std::uint64_t maxAdditionalSamples{256};
    double maxStrokeWidth{3.0};
    std::uint32_t gridStep{4};
};
struct QualityProgress {
    QualityStatus status{QualityStatus::NeedsReview};
    double colorMae{};
    double alphaMae{};
    double outsideAlphaMae{};
    std::uint64_t evaluatedPixels{};
    std::uint64_t remainingPixels{};
};
struct RefinementRequest {
    PlanningRegion region;
    DocumentSnapshot painted;
    QualityBudget budget{};
    std::stop_token stop{};
};
Result<QualityProgress> measureRegionProgress(const RefinementRequest&);
Result<EventBatch> refineRegion(const RefinementRequest&, std::stop_token = {});
}
