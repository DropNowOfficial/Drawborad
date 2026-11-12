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
    std::uint8_t tolerance, ColorValue color, std::stop_token stop, std::uint32_t underInkPixels) {
    if(underInkPixels>2)return error(ErrorCode::InvalidInput,"bucket.under-ink-distance");
    auto valid=validateSnapshot(s);if (!valid) return valid.error();
    if (!nativeLayerSupported(s,layer))
        return error(ErrorCode::UnsupportedFeature,"bucket.layer");
    if (layerLocked(s,layer)) return error(ErrorCode::Locked,"bucket.locked");
    if (!nativeLayerPaintable(s,layer))
        return error(ErrorCode::UnsupportedFeature,"bucket.hidden-or-transformed");
    for (auto id=layer;!id.empty();id=s.nodes->at(id).parent)
        if (!s.nodes->at(id).maskId.empty())
            return error(ErrorCode::UnsupportedFeature,"bucket.masked");
    std::span<const std::uint8_t> underInk;
    if(underInkPixels){
        // First version accepts exactly one visible, unmasked original raster
        // above a root-level color layer, not arbitrary compositing stacks.
        if(s.nodes->at(layer).parent!=Document::rootId())return error(ErrorCode::UnsupportedFeature,"bucket.under-ink-stack");
        bool above=false,found=false;
        for(const auto id:layerOrder(s)){
            if(id==layer){above=true;continue;}
            if(!above || !layerVisible(s,id))continue;
            const auto& ink=s.nodes->at(id);
            if(found || ink.parent!=Document::rootId() || ink.stage!=LineartStage || !ink.locked || ink.opacity!=1 || !ink.maskId.empty() || ink.transform!=LayerTransform{})
                return error(ErrorCode::UnsupportedFeature,"bucket.under-ink-stack");
            auto raw=lineartRasterPixels(s,id);if(!raw)return raw.error();underInk=raw.value();found=true;
        }
        if(!found)return error(ErrorCode::UnsupportedFeature,"bucket.under-ink-source-missing");
        // Under-ink is specifically for black/gray linework, not colored paper
        // or painted images. Invisible RGB is immaterial and may be arbitrary.
        for(std::size_t i=0;i<underInk.size();i+=4){
            if((i%65536)==0 && stop.stop_requested())return error(ErrorCode::Cancelled,"bucket.under-ink-cancelled");
            if(underInk[i+3] && (underInk[i]!=underInk[i+1] || underInk[i+1]!=underInk[i+2]))
                return error(ErrorCode::UnsupportedFeature,"bucket.under-ink-grayscale-required");
        }
    }
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
        if(underInkPixels){
            const auto seed=(std::size_t(y)*reference.width+x)*4;
            if(underInk[seed+3] && (underInk[seed]!=255 || underInk[seed+1]!=255 || underInk[seed+2]!=255))
                return error(ErrorCode::UnsupportedFeature,"bucket.under-ink-white-or-transparent-seed-required");
            // Flood's temporary visited storage is gone; release RGBA before
            // allocating one byte/pixel generation mask. No frontier O(N) list.
            std::vector<Rgba8>().swap(rgba);
            const auto w=reference.width,h=reference.height;
            std::vector<std::uint8_t> selected(std::size_t(w)*h,0);
            for(const auto run:filled.value().runs)std::fill_n(selected.begin()+std::size_t(run.y)*w+run.x,run.length,1);
            for(std::uint32_t round=1;round<=underInkPixels;round++){
                for(std::uint32_t row=0;row<h;row++){
                    if(stop.stop_requested())return error(ErrorCode::Cancelled,"bucket.under-ink-cancelled");
                    for(std::uint32_t col=0;col<w;col++){
                        const auto i=std::size_t(row)*w+col;
                        if(selected[i] || !underInk[i*4+3] || (underInk[i*4]==255 && underInk[i*4+1]==255 && underInk[i*4+2]==255))continue;
                        const auto prior=[&](std::size_t n){return selected[n]>0 && selected[n]<=round;};
                        if((col&&prior(i-1))||(col+1<w&&prior(i+1))||(row&&prior(i-w))||(row+1<h&&prior(i+w)))selected[i]=std::uint8_t(round+1);
                    }
                }
            }
            auto& result=filled.value();result.runs.clear();result.pixelCount=0;
            for(std::uint32_t row=0;row<h;row++){
                if(stop.stop_requested())return error(ErrorCode::Cancelled,"bucket.under-ink-cancelled");
                for(std::uint32_t col=0;col<w;){
                    if(!selected[std::size_t(row)*w+col]){col++;continue;}
                    const auto begin=col;while(col<w && selected[std::size_t(row)*w+col])col++;
                    if(result.runs.size()>=FillBudget{}.maxRuns)return error(ErrorCode::BudgetExceeded,"bucket.under-ink-runs");
                    result.runs.push_back({row,begin,col-begin});result.pixelCount+=col-begin;
                }
            }
        }
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
