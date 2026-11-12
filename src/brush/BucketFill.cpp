#include "BucketFill.h"
#include "view/LayerTree.h"
#include "ui/MergedDocument.h"
#include <algorithm>
namespace drawborad {
Result<Command> addFillLayerCommand(const DocumentSnapshot& s, NodeId id, bool mergedPreview, bool belowInk) {
    auto valid=validateSnapshot(s);if (!valid) return valid.error();
    const auto size=s.spec.pixelSize;
    // Match the current full-document preview budget before changing state.
    if(!mergedPreview && (size.width>4096 || size.height>4096 || size.width*size.height>4*1024*1024 ||
       size.width*size.height*4*((s.nodes->size()+1)*2+2)>128*1024*1024))
        return error(ErrorCode::BudgetExceeded,"bucket.new-layer-layout-limit");
    if(id.empty() || s.nodes->contains(id))
        return error(ErrorCode::InvalidInput,"bucket.layer-id");
    if(s.nodes->at(Document::rootId()).locked)
        return error(ErrorCode::Locked,"bucket.root-locked");
    LayerNode layer;layer.id=id;layer.parent=Document::rootId();layer.name="Color fill";
    layer.bounds={0,0,s.spec.pixelSize.width,s.spec.pixelSize.height};
    Command command(AddNode{layer,belowInk?0:SIZE_MAX});
    if(mergedPreview){
        auto candidate=Document::fromSnapshot(s);if(!candidate)return candidate.error();
        auto applied=candidate.value().apply(command,s.revision);if(!applied)return applied.error();
        auto plan=planMergedDocument(candidate.value().snapshot());if(!plan)return plan.error();
    }
    return command;
}
Result<Command> bucketFillCommand(const DocumentSnapshot& s, NodeId layer, Id event,
    const TileData& reference, std::uint32_t x, std::uint32_t y,
    std::uint8_t tolerance, ColorValue color, std::stop_token stop) {
    auto valid=validateSnapshot(s);if (!valid) return valid.error();
    if (!nativeLayerSupported(s,layer))
        return error(ErrorCode::UnsupportedFeature,"bucket.layer");
    if (layerLocked(s,layer)) return error(ErrorCode::Locked,"bucket.locked");
    if (!nativeLayerPaintable(s,layer))
        return error(ErrorCode::UnsupportedFeature,"bucket.hidden-or-transformed");
    for (auto id=layer;!id.empty();id=s.nodes->at(id).parent)
        if (!s.nodes->at(id).maskId.empty())
            return error(ErrorCode::UnsupportedFeature,"bucket.masked");
    if (event.empty() || std::any_of(s.eventIndex->begin(),s.eventIndex->end(),
        [&](const auto& e){return e.id==event;}))
        return error(ErrorCode::InvalidInput,"bucket.event-id");
    if (reference.format!=PixelFormat{ColorMode::RGB,Depth::U8,true} ||
        reference.width!=s.spec.pixelSize.width || reference.height!=s.spec.pixelSize.height ||
        reference.rowStride!=std::uint64_t(reference.width)*4 ||
        reference.bytes.size()!=reference.rowStride*reference.height ||
        reference.profileId!=s.spec.colorSpec.profileId)
        return error(ErrorCode::InvalidInput,"bucket.reference");
    if (reference.bytes.size()/4>FillBudget{}.maxPixels || s.eventIndex->size()>=8192)
        return error(ErrorCode::BudgetExceeded,"bucket.document-budget");
    std::uint64_t points=0;
    std::uint64_t tick=0;
    for (const auto& e:*s.eventIndex) {
        if (e.geometry.size()>1000000-points)
            return error(ErrorCode::BudgetExceeded,"bucket.document-budget");
        points+=e.geometry.size();
        if (e.endUs==UINT64_MAX) return error(ErrorCode::Overflow,"bucket.timeline");
        tick=std::max(tick,e.endUs+1);
    }
    try {
        std::vector<Rgba8> rgba(reference.bytes.size()/4);
        for (std::size_t i=0;i<rgba.size();++i)
            rgba[i]={reference.bytes[4*i],reference.bytes[4*i+1],
                     reference.bytes[4*i+2],reference.bytes[4*i+3]};
        auto filled=floodFill(rgba,reference.width,reference.height,x,y,tolerance,{},stop);
        if (!filled) return filled.error();
        if (filled.value().runs.size()*2>1000000-points)
            return error(ErrorCode::BudgetExceeded,"bucket.document-budget");
        StrokeEvent e;e.id=event;e.objectId=s.nodes->at(layer).objectId;e.layerId=layer;
        e.engineVersion=8;e.toolId="bucket";e.parentRevision=s.revision;e.color=color;
        e.startUs=tick;e.endUs=tick;
        for (const auto run:filled.value().runs) {
            e.geometry.push_back({double(run.x),double(run.y),1,{}, {},tick,0,0});
            e.geometry.push_back({double(run.x+run.length),double(run.y),1,{}, {},tick,0,0});
        }
        return Command(Paint{std::move(e),{0,0,reference.width,reference.height}});
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded,"bucket.allocation");
    }
}
}
