#pragma once
#include "core/Document.h"
#include "review/ReviewSession.h"
#include <stop_token>

namespace drawborad {

// Source pixels are available only during compilation, never in an EventBatch.
struct ImmutableSource {
    const TileData* pixels{};
};

struct ReconstructionBudget {
    std::uint64_t maxEvents{4096};
    std::uint64_t maxSamples{500000};
    double maxStrokeWidth{128.0};
};

struct EventBatch {
    Revision inputRevision{};
    NodeId objectId{};
    NodeId layerId{};
    std::vector<StrokeEvent> events;
};

struct ReconstructionRequest {
    DocumentSnapshot snapshot;
    ReviewedObjects reviewed;
    ImmutableSource source;
    std::size_t maskIndex{};
    NodeId objectId{};
    NodeId layerId{};
    ReconstructionBudget budget{};
    std::vector<StrokeEvent> proposedEvents;
};

struct ReconstructionResult {
    EventBatch staged;
};

Result<EventBatch> validateReconstructionBatch(const EventBatch&);
Result<ChangeSet> commitReconstructionBatch(Document&, const EventBatch&,
                                            std::stop_token = {});

class ReconstructionService {
public:
    Result<ReconstructionResult> compile(const ReconstructionRequest&,
                                         std::stop_token = {}) const;
};

} // namespace drawborad
