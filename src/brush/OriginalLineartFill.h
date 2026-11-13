#pragma once
#include "BucketFill.h"
#include "core/LineartRaster.h"
#include "view/LayerTree.h"
namespace drawborad {
// Bounded synchronous desktop path. Samples the declared original only, never
// the colored composite. Returns a command; it does not mutate its source.
inline Result<Command> originalLineartFillCommand(const DocumentSnapshot& s,
 NodeId source,NodeId target,Id event,std::uint32_t x,std::uint32_t y,
 std::uint8_t tolerance,ColorValue color,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"original-fill.cancelled");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(!w||!h||w>4096||h>4096||w*h>4*1024*1024)
  return error(ErrorCode::BudgetExceeded,"original-fill.desktop-limit");
 if(s.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})
  return error(ErrorCode::UnsupportedFeature,"original-fill.rgba8-required");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 const auto& root=s.nodes->at(Document::rootId());
 if(root.children!=std::vector<NodeId>{target,source}||s.nodes->size()!=3||source==target)
  return error(ErrorCode::UnsupportedFeature,"original-fill.two-leaf-stack");
 const auto& ink=s.nodes->at(source);const auto& fill=s.nodes->at(target);
 if(ink.kind!=NodeKind::Layer||!ink.locked||!layerVisible(s,source)||ink.opacity!=1||
    !ink.maskId.empty()||ink.transform!=LayerTransform{}||
    fill.kind!=NodeKind::Layer||fill.blend!=BlendMode::Normal||fill.opacity!=1||
    !nativeLayerPaintable(s,target)||!nativeLayerSupported(s,target)||!fill.maskId.empty())
  return error(ErrorCode::UnsupportedFeature,"original-fill.stack-properties");
 if(x>=w||y>=h)return error(ErrorCode::InvalidInput,"original-fill.seed");
 auto raw=lineartRasterPixels(s,source);if(!raw)return raw.error();
 for(std::size_t i=0;i<raw.value().size();i+=4){
  if(i%65536==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"original-fill.cancelled");
  const auto p=raw.value().subspan(i,4);
  if(p[3]&&(p[0]!=p[1]||p[1]!=p[2]))return error(ErrorCode::UnsupportedFeature,"original-fill.grayscale-required");
 }
 const auto offset=(std::size_t(y)*w+x)*4;const auto seed=raw.value().subspan(offset,4);
 if(seed[3]&&seed[0]!=255)return error(ErrorCode::InvalidInput,"original-fill.white-or-transparent-seed");
 TileData reference;reference.width=std::uint32_t(w);reference.height=std::uint32_t(h);
 reference.rowStride=w*4;reference.format=s.spec.colorSpec.format;reference.profileId=s.spec.colorSpec.profileId;
 reference.bytes.assign(raw.value().begin(),raw.value().end());
 return bucketFillCommand(s,target,event,reference,x,y,tolerance,color,stop);
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"original-fill.allocation");}
inline Result<Command> originalLineartFillCommand(const DocumentSnapshot& s,
 NodeId source,NodeId target,Id event,std::uint32_t x,std::uint32_t y,ColorValue color,
 std::stop_token stop={}) {
 return originalLineartFillCommand(s,source,target,event,x,y,0,color,stop);
}

}
