#include "ReconstructionService.h"
#include "core/BucketEvent.h"
#include "core/ChiselInkEvent.h"
#include "core/DryInkEvent.h"
#include "core/FreehandEvent.h"
#include <cmath>
#include <set>
#include <string_view>

namespace drawborad {
namespace {
bool supportedPigment(std::string_view id) {
    return id == "pencil" || id == "colored-pencil" || id == "fineliner" ||
           id == "marker" || id == "highlight" || id == "guide" || id == "eraser" ||
           id == "bucket" || id == "fineliner-aa" || id == "dry-ink" ||
           id == "smooth-ink-aa" || id == "chisel-ink-aa";
}
bool unit(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}
}
Result<EventBatch> validateReconstructionBatch(const EventBatch& batch) {
    if (batch.objectId.empty() || batch.layerId.empty() || batch.events.empty() ||
        batch.events.size() > 65536)
        return error(ErrorCode::InvalidInput, "reconstruct.batch-layout");
    std::set<Id> seen;
    std::uint64_t previousEnd = 0;
    bool first = true;
    for (const auto& event : batch.events) {
        if (!first && event.startUs < previousEnd)
            return error(ErrorCode::InvalidInput, "reconstruct.event-order");
        first = false;
        previousEnd = event.endUs;
        if (event.id.empty() || !seen.insert(event.id).second ||
            event.objectId != batch.objectId || event.layerId != batch.layerId)
            return error(ErrorCode::InvalidInput, "reconstruct.event-identity");
        if (event.parentRevision != batch.inputRevision)
            return error(ErrorCode::StaleRevision, "reconstruct.event-revision");
        if (!supportedPigment(event.toolId))
            return error(ErrorCode::UnsupportedFeature, "reconstruct.non-pigment-event");
        if ((event.color.activeChannels != 3 && event.color.activeChannels != 4) ||
            !unit(event.color.alpha))
            return error(ErrorCode::InvalidInput, "reconstruct.event-color");
        for (unsigned channel = 0; channel < event.color.activeChannels; ++channel) {
            if (!unit(event.color.channels[channel]))
                return error(ErrorCode::InvalidInput, "reconstruct.event-color");
        }
        if (event.engineVersion == 8) {
            auto ok = validateBucketEvent(event);
            if (!ok) return ok.error();
            continue;
        }
        if (event.engineVersion == 10) {
            auto ok = validateFreehandEvent(event);
            if (!ok) return ok.error();
            continue;
        }
        if (event.engineVersion == 12) {
            auto ok = validateDryInkEvent(event);
            if (!ok) return ok.error();
            continue;
        }
        if (event.engineVersion == 19) {
            auto ok = validateSmoothInkEvent(event);
            if (!ok) return ok.error();
            continue;
        }
        if (event.engineVersion == 21) {
            auto ok = validateChiselInkEvent(event);
            if (!ok) return ok.error();
            continue;
        }
        if ((event.engineVersion != 1 && event.engineVersion != 2) ||
            event.toolId == "bucket" || event.toolId == "fineliner-aa" ||
            event.toolId == "dry-ink" || event.toolId == "smooth-ink-aa" ||
            event.toolId == "chisel-ink-aa" ||
            event.geometry.empty() || event.geometry.size() > 1000000 ||
            event.startUs > event.endUs ||
            !std::isfinite(event.style.width) || event.style.width <= 0 ||
            event.style.width > 8192 || !unit(event.style.opacity) ||
            !unit(event.style.hardness) ||
            (!event.style.grain.empty() && event.style.grain.size() != 64))
            return error(ErrorCode::InvalidInput, "reconstruct.event-geometry");
        auto last = event.startUs;
        for (const auto& point : event.geometry) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !unit(point.pressure) || point.timestampUs < last ||
                point.timestampUs > event.endUs ||
                (point.tiltX && !std::isfinite(*point.tiltX)) ||
                (point.tiltY && !std::isfinite(*point.tiltY)))
                return error(ErrorCode::InvalidInput, "reconstruct.event-sample");
            last = point.timestampUs;
        }
    }
    return batch;
}
} // namespace drawborad
