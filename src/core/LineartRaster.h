#pragma once
#include "Commands.h"
#include <algorithm>
#include <span>
namespace drawborad {
inline constexpr char LineartStage[]="drawborad.lineart.rgba.v1";
Result<Command> makeLineartRasterCommand(Bytes rgba,Bytes originalPng,
    std::uint32_t width,std::uint32_t height,NodeId id);
// Borrowed immutable view; lifetime is the snapshot's resource lifetime.
inline Result<std::span<const std::uint8_t>> lineartRasterPixels(const DocumentSnapshot& s,NodeId id){
 if(!s.nodes||!s.resources)return error(ErrorCode::CorruptData,"lineart.structure");
 constexpr std::uint8_t magic[]={'D','B','L','R','G','B','A','1'};
 const auto get=[](const Bytes& b,unsigned p){std::uint32_t n=0;for(unsigned i=0;i<4;i++)n|=std::uint32_t(b[p+i])<<(i*8);return n;};
 const auto it=s.nodes->find(id);if(it==s.nodes->end())return error(ErrorCode::InvalidInput,"lineart.node");
 const auto& n=it->second;
 if(n.kind!=NodeKind::Layer||n.sourceKind!=SourceKind::ImportedRaster||n.stage!=LineartStage||n.blend!=BlendMode::Multiply||!n.content.empty()||!n.children.empty()||
    n.bounds!=Rect{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height})return error(ErrorCode::UnsupportedFeature,"lineart.node-layout");
 const auto resource=s.resources->find(n.sourceResource);
 if(resource==s.resources->end()||resource->second.kind!=ResourceKind::Reference||!resource->second.bytes)return error(ErrorCode::CorruptData,"lineart.resource");
 const auto& raw=resource->second;const auto& b=*raw.bytes;
 if(b.size()<16||!std::equal(std::begin(magic),std::end(magic),b.begin()))return error(ErrorCode::CorruptData,"lineart.header");
 auto w=get(b,8),h=get(b,12);const auto pixels=std::uint64_t(w)*h;
 if(!w||!h||w!=s.spec.pixelSize.width||h!=s.spec.pixelSize.height||pixels>(MaxResourceBytes-16)/4||b.size()!=16+pixels*4||raw.dependencies.size()!=1)
  return error(ErrorCode::CorruptData,"lineart.rgba-layout");
 const auto png=s.resources->find(raw.dependencies.front());
 if(png==s.resources->end()||png->second.kind!=ResourceKind::Reference||!png->second.bytes||png->second.bytes->empty())return error(ErrorCode::CorruptData,"lineart.original-missing");
 return std::span<const std::uint8_t>(b).subspan(16);
}
}
