#pragma once
#include "core/Document.h"
#include "core/LineartRaster.h"
#include <algorithm>
namespace drawborad {
// Document child order is the painting order, from bottom to top.
inline std::vector<NodeId> layerOrder(const DocumentSnapshot& snapshot) {
    std::vector<NodeId> result, pending{Document::rootId()};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto found = snapshot.nodes->find(id);
        if (found == snapshot.nodes->end()) continue;
        const auto& node = found->second;
        if (node.kind == NodeKind::Layer) result.push_back(id);
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
            pending.push_back(*it);
    }
    return result;
}
inline bool layerVisible(const DocumentSnapshot& snapshot, NodeId id) {
    while (!id.empty()) {
        const auto found = snapshot.nodes->find(id);
        if (found == snapshot.nodes->end() || !found->second.visible) return false;
        id = found->second.parent;
    }
    return true;
}
inline bool layerWritable(const DocumentSnapshot& snapshot, NodeId id) {
    while (!id.empty()) {
        const auto found = snapshot.nodes->find(id);
        if (found == snapshot.nodes->end() || found->second.locked ||
            found->second.transform != LayerTransform{}) return false;
        id = found->second.parent;
    }
    return true;
}
inline bool layerLocked(const DocumentSnapshot& snapshot, NodeId id) {
    while (!id.empty()) {
        const auto found = snapshot.nodes->find(id);
        if (found == snapshot.nodes->end() || found->second.locked) return true;
        id = found->second.parent;
    }
    return false;
}
inline bool nativeCompositingSupported(const DocumentSnapshot& snapshot, NodeId id) {
    bool masked=false,transformed=false;
    while (!id.empty()) {
        const auto found = snapshot.nodes->find(id);
        if (found == snapshot.nodes->end()) return false;
        const auto& node = found->second;
        const bool lineart=node.stage==LineartStage && bool(lineartRasterPixels(snapshot,id));
        if (!lineart && (!node.content.empty() || node.sourceKind != SourceKind::Reconstructed ||
            !node.sourceResource.empty() || node.blend != BlendMode::Normal ||
            (node.kind != NodeKind::Layer && node.opacity != 1))) return false;
        transformed=transformed || node.transform!=LayerTransform{};
        if(!node.maskId.empty()){
            const auto pixels=checkedMultiply(snapshot.spec.pixelSize.width,snapshot.spec.pixelSize.height);
            const auto mask=snapshot.resources->find(node.maskId);
            if(!pixels || mask==snapshot.resources->end() || mask->second.kind!=ResourceKind::Mask ||
               mask->second.bytes->size()!=pixels.value())return false;
            // Binary ancestor masks commute with per-layer compositing. Soft
            // group masks require an offscreen group composite, not independent
            // attenuation of each child layer (which would accumulate opacity).
            if(node.kind!=NodeKind::Layer && !std::all_of(mask->second.bytes->begin(),mask->second.bytes->end(),
                [](auto value){return value==0 || value==255;}))return false;
            masked=true;
        }
        id = node.parent;
    }
    return !masked || !transformed;
}
// Apply masks to a display/export copy, leaving raw stroke tiles and replay
// checkpoints untouched. Reapplying a soft mask to a checkpoint would darken it.
inline Result<TileData> nativeMaskedTile(const DocumentSnapshot& snapshot,NodeId id,
                                        TileData tile,std::int64_t originX,std::int64_t originY){
    if(!nativeCompositingSupported(snapshot,id) || tile.format!=PixelFormat{ColorMode::RGB,Depth::U8,true} ||
       tile.rowStride<std::uint64_t(tile.width)*4 || tile.bytes.size()<tile.rowStride*tile.height)
        return error(ErrorCode::UnsupportedFeature,"display.mask-layout");
    std::vector<const Bytes*> masks;
    while(!id.empty()){
        const auto& node=snapshot.nodes->at(id);
        if(!node.maskId.empty())masks.push_back(snapshot.resources->at(node.maskId).bytes.get());
        id=node.parent;
    }
    if(masks.empty())return tile;
    for(std::uint32_t y=0;y<tile.height;++y)for(std::uint32_t x=0;x<tile.width;++x){
        const auto dx=originX+std::int64_t(x),dy=originY+std::int64_t(y);
        auto& alpha=tile.bytes[std::size_t(y*tile.rowStride+x*4+3)];
        if(dx<0 || dy<0 || std::uint64_t(dx)>=snapshot.spec.pixelSize.width || std::uint64_t(dy)>=snapshot.spec.pixelSize.height){alpha=0;continue;}
        const auto pixel=std::size_t(std::uint64_t(dy)*snapshot.spec.pixelSize.width+std::uint64_t(dx));
        for(const auto mask:masks)alpha=std::uint8_t((unsigned(alpha)*(*mask)[pixel]+127)/255);
    }
    return tile;
}
inline bool nativeLayerSupported(const DocumentSnapshot& snapshot, NodeId id) {
    const auto format = snapshot.spec.colorSpec.format;
    if (format.mode != ColorMode::RGB || format.depth != Depth::U8 || !format.alpha) return false;
    const auto found = snapshot.nodes->find(id);
    return found != snapshot.nodes->end() && found->second.kind == NodeKind::Layer &&
        found->second.sourceKind == SourceKind::Reconstructed && found->second.content.empty() &&
        nativeCompositingSupported(snapshot, id);
}
inline bool nativeLayerPaintable(const DocumentSnapshot& snapshot, NodeId id) {
    return nativeLayerSupported(snapshot, id) && layerVisible(snapshot, id) && layerWritable(snapshot, id);
}
} // namespace drawborad
