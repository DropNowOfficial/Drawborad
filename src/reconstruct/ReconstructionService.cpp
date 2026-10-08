#include "ReconstructionService.h"
#include "core/Commands.h"
#include "render/ReferenceRenderer.h"
#include <cmath>
#include <set>
#include <limits>

namespace drawborad {
namespace {
Result<void> checkTarget(const DocumentSnapshot& snapshot, NodeId objectId,
                         NodeId layerId) {
    if (!snapshot.nodes)
        return error(ErrorCode::InvalidInput, "reconstruct.document-nodes");
    const auto object = snapshot.nodes->find(objectId);
    const auto layer = snapshot.nodes->find(layerId);
    if (object == snapshot.nodes->end() || object->second.kind != NodeKind::Object ||
        layer == snapshot.nodes->end() || layer->second.kind != NodeKind::Layer ||
        layer->second.objectId != objectId)
        return error(ErrorCode::InvalidInput, "reconstruct.object-layer");
    NodeId cursor = layerId;
    std::size_t walked = 0;
    while (cursor != objectId) {
        if (++walked > snapshot.nodes->size())
            return error(ErrorCode::InvalidInput, "reconstruct.object-layer");
        const auto it = snapshot.nodes->find(cursor);
        if (it == snapshot.nodes->end() || it->second.parent.empty())
            return error(ErrorCode::InvalidInput, "reconstruct.object-layer");
        if (it->second.locked)
            return error(ErrorCode::Locked, "reconstruct.locked-layer");
        if (cursor != layerId && it->second.kind != NodeKind::Group)
            return error(ErrorCode::InvalidInput, "reconstruct.object-layer");
        cursor = it->second.parent;
    }
    if (object->second.locked)
        return error(ErrorCode::Locked, "reconstruct.locked-layer");
    if (layer->second.sourceKind != SourceKind::Reconstructed ||
        !layer->second.content.empty() || !layer->second.sourceResource.empty())
        return error(ErrorCode::UnsupportedFeature, "reconstruct.source-backed-layer");
    return {};
}
Result<void> checkSource(const ReconstructionRequest& request) {
    if (!request.source.pixels)
        return error(ErrorCode::InvalidInput, "reconstruct.source-required");
    const auto& source = *request.source.pixels;
    const auto size = request.snapshot.spec.pixelSize;
    if (source.width != size.width || source.height != size.height ||
        source.format != request.snapshot.spec.colorSpec.format ||
        source.profileId != request.snapshot.spec.colorSpec.profileId)
        return error(ErrorCode::InvalidInput, "reconstruct.source-layout");
    auto pixelBytes = checkedMultiply(std::uint64_t(source.format.channelCount()),
                                      source.format.depth == Depth::U16 ? 2u : 1u);
    if (!pixelBytes)
        return pixelBytes.error();
    auto rowBytes = checkedMultiply(size.width, pixelBytes.value());
    if (!rowBytes)
        return rowBytes.error();
    auto totalBytes = checkedMultiply(source.rowStride, size.height);
    if (!totalBytes)
        return totalBytes.error();
    if (source.rowStride < rowBytes.value() || totalBytes.value() > source.bytes.size())
        return error(ErrorCode::InvalidInput, "reconstruct.source-layout");
    return {};
}
}

Result<ReconstructionResult> ReconstructionService::compile(
    const ReconstructionRequest& request, std::stop_token stop) const {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "reconstruct.cancelled");
    const auto& snapshot = request.snapshot;
    if (request.reviewed.confirmedRevision != snapshot.revision)
        return error(ErrorCode::StaleRevision, "reconstruct.review-revision");
    if (!request.reviewed.canReconstruct || request.maskIndex >= request.reviewed.masks.size())
        return error(ErrorCode::InvalidInput, "reconstruct.unreviewed");
    auto target = checkTarget(snapshot, request.objectId, request.layerId);
    if (!target)
        return target.error();
    auto source = checkSource(request);
    if (!source)
        return source.error();
    const auto size = snapshot.spec.pixelSize;
    auto pixels = checkedMultiply(size.width, size.height);
    if (!pixels)
        return pixels.error();
    const auto& mask = request.reviewed.masks[request.maskIndex];
    if (pixels.value() != mask.size())
        return error(ErrorCode::InvalidInput, "reconstruct.review-mask-layout");
    if (request.budget.maxEvents == 0 || request.budget.maxEvents > 65536 ||
        request.budget.maxSamples == 0 ||
        !std::isfinite(request.budget.maxStrokeWidth) ||
        request.budget.maxStrokeWidth <= 0 || request.budget.maxStrokeWidth > 8192 ||
        request.proposedEvents.size() > request.budget.maxEvents)
        return error(ErrorCode::BudgetExceeded, "reconstruct.event-budget");
    EventBatch batch{snapshot.revision, request.objectId, request.layerId,
                     request.proposedEvents};
    auto valid = validateReconstructionBatch(batch);
    if (!valid)
        return valid.error();
    std::set<Id> existing;
    if (snapshot.eventIndex) {
        for (const auto& e : *snapshot.eventIndex)
            existing.insert(e.id);
    }
    std::uint64_t totalSamples = 0;
    const auto format = snapshot.spec.colorSpec;
    for (const auto& event : batch.events) {
        if (stop.stop_requested())
            return error(ErrorCode::Cancelled, "reconstruct.cancelled");
        if (existing.contains(event.id))
            return error(ErrorCode::InvalidInput, "reconstruct.duplicate-event");
        if (event.color.profileId != format.profileId ||
            event.color.activeChannels !=
                (format.format.mode == ColorMode::CMYK ? 4u : 3u))
            return error(ErrorCode::InvalidInput, "reconstruct.document-color");
        if (event.style.width > request.budget.maxStrokeWidth ||
            event.geometry.size() > request.budget.maxSamples - totalSamples)
            return error(ErrorCode::BudgetExceeded, "reconstruct.stroke-budget");
        totalSamples += event.geometry.size();
        for (const auto& point : event.geometry) {
            if (point.x < 0 || point.y < 0 || point.x >= double(size.width) ||
                point.y >= double(size.height))
                return error(ErrorCode::InvalidInput, "reconstruct.outside-source");
            const auto x = std::uint64_t(point.x);
            const auto y = std::uint64_t(point.y);
            const auto index = y * size.width + x;
            if (mask[std::size_t(index)] == 0)
                return error(ErrorCode::InvalidInput, "reconstruct.outside-reviewed-mask");
        }
    }
    // No source pixel data, pointer, or source asset handle escapes this boundary.
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "reconstruct.cancelled");
    return ReconstructionResult{std::move(batch)};
}

Result<ChangeSet> commitReconstructionBatch(Document& document,
                                             const EventBatch& batch,
                                             std::stop_token stop) {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "reconstruct.cancelled");
    const auto snapshot = document.snapshot();
    if (snapshot.revision != batch.inputRevision)
        return error(ErrorCode::StaleRevision, "reconstruct.commit-revision");
    auto valid = validateReconstructionBatch(batch);
    if (!valid)
        return valid.error();
    auto target = checkTarget(snapshot, batch.objectId, batch.layerId);
    if (!target)
        return target.error();
    std::set<Id> old;
    for (const auto& item : *snapshot.eventIndex)
        old.insert(item.id);
    Command command;
    command.operations.reserve(batch.events.size());
    for (const auto& event : batch.events) {
        if (stop.stop_requested())
            return error(ErrorCode::Cancelled, "reconstruct.cancelled");
        if (old.contains(event.id))
            return error(ErrorCode::InvalidInput, "reconstruct.duplicate-event");
        if (event.color.profileId != snapshot.spec.colorSpec.profileId ||
            event.color.activeChannels !=
                (snapshot.spec.colorSpec.format.mode == ColorMode::CMYK ? 4u : 3u))
            return error(ErrorCode::InvalidInput, "reconstruct.document-color");
        auto bounds = strokeBounds(event);
        if (!bounds)
            return bounds.error();
        command.operations.emplace_back(Paint{event, bounds.value()});
    }
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "reconstruct.cancelled");
    // Document::apply checks the revision again and commits all paints in one transaction.
    return document.apply(command, batch.inputRevision);
}
} // namespace drawborad
