#pragma once
#include "ReconstructionService.h"
#include <span>
#include <string>

namespace drawborad {

// E02 reads the reviewed mask and color-managed source temporarily. Neither is
// persisted in the resulting EventBatch. Bounds refer to document coordinates.
struct PlanningRegion {
    DocumentSnapshot snapshot;
    NodeId objectId{};
    NodeId layerId{};
    Rect bounds{};
    std::span<const std::uint8_t> reviewedMask;
    const TileData* source{};
    std::uint64_t seed{1};
    std::uint64_t maxEvents{4096};
    std::uint64_t maxSamples{250000};
    std::stop_token stop{};
    // Only for a matching persisted native binary object mask. The renderer
    // clips brush footprints; ordinary planning retains conservative erosion.
    bool clipToReviewedMask{};
};

struct StyleSettings {
    std::string toolId{"marker"};
    double width{2.0};
    double spacing{5.0};
    double opacity{1.0};
    // Zero retains the original long-stroke behavior. Positive values bound
    // within-stroke source color variation by splitting coherent sweep runs.
    double maxColorVariation{};
};

struct SchedulePolicy {
    std::vector<Rect> regionOrder;
    std::uint64_t seed{1};
    std::uint64_t startUs{100};
    std::uint64_t sampleStepUs{100};
    std::uint64_t gapUs{50};
    std::uint64_t maxEvents{4096};
};

Result<EventBatch> planContours(const PlanningRegion&);
Result<EventBatch> planBasePigment(const PlanningRegion&, const StyleSettings&);
Result<EventBatch> scheduleStrokes(const EventBatch&, const SchedulePolicy&);

} // namespace drawborad
