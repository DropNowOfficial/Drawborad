#include "LineartRaster.h"
#include "Digest.h"
#include <algorithm>
namespace drawborad {
namespace {
constexpr std::uint8_t magic[]={'D','B','L','R','G','B','A','1'};
void put(Bytes& b,std::uint32_t n){for(unsigned i=0;i<4;i++)b.push_back(std::uint8_t(n>>(i*8)));}
std::uint32_t get(const Bytes& b,unsigned p){std::uint32_t n=0;for(unsigned i=0;i<4;i++)n|=std::uint32_t(b[p+i])<<(i*8);return n;}
}
Result<Command> makeLineartRasterCommand(Bytes rgba,Bytes originalPng,std::uint32_t w,std::uint32_t h,NodeId id){
 const auto n=std::uint64_t(w)*h;
 if(!w||!h||n>(64*1024*1024-16)/4||rgba.size()!=n*4||id.empty()||originalPng.empty()||originalPng.size()>64*1024*1024)
  return error(ErrorCode::InvalidInput,"lineart.rgba-layout");
 try{
  Resource png;png.kind=ResourceKind::Reference;png.id=sha256(originalPng);png.bytes=std::make_shared<const Bytes>(std::move(originalPng));
  Bytes data;data.reserve(16+rgba.size());data.insert(data.end(),std::begin(magic),std::end(magic));put(data,w);put(data,h);data.insert(data.end(),rgba.begin(),rgba.end());
  Resource raw;raw.kind=ResourceKind::Reference;raw.id=sha256(data);raw.bytes=std::make_shared<const Bytes>(std::move(data));raw.dependencies={png.id};
  LayerNode node;node.id=id;node.parent=Document::rootId();node.name="Original lineart";node.stage=LineartStage;
  node.sourceKind=SourceKind::ImportedRaster;node.sourceResource=raw.id;node.bounds={0,0,w,h};node.blend=BlendMode::Multiply;node.locked=true;
  Command command;command.operations={InstallResource{png},InstallResource{raw},AddNode{node}};return command;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"lineart.allocation");}
}
Result<std::span<const std::uint8_t>> lineartRasterPixels(const DocumentSnapshot& s,NodeId id){
 if(!s.nodes||!s.resources)return error(ErrorCode::CorruptData,"lineart.structure");
 const auto it=s.nodes->find(id);if(it==s.nodes->end())return error(ErrorCode::InvalidInput,"lineart.node");
 const auto& n=it->second;
 if(n.kind!=NodeKind::Layer||n.sourceKind!=SourceKind::ImportedRaster||n.stage!=LineartStage||n.blend!=BlendMode::Multiply||!n.content.empty()||!n.children.empty()||
    n.bounds!=Rect{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height})return error(ErrorCode::UnsupportedFeature,"lineart.node-layout");
 const auto resource=s.resources->find(n.sourceResource);
 if(resource==s.resources->end()||resource->second.kind!=ResourceKind::Reference||!resource->second.bytes)return error(ErrorCode::CorruptData,"lineart.resource");
 const auto& raw=resource->second;const auto& b=*raw.bytes;
 if(b.size()<16||!std::equal(std::begin(magic),std::end(magic),b.begin()))return error(ErrorCode::CorruptData,"lineart.header");
 auto w=get(b,8),h=get(b,12);const auto pixels=std::uint64_t(w)*h;
 if(!w||!h||w!=s.spec.pixelSize.width||h!=s.spec.pixelSize.height||pixels>(64*1024*1024-16)/4||b.size()!=16+pixels*4||raw.dependencies.size()!=1)
  return error(ErrorCode::CorruptData,"lineart.rgba-layout");
 const auto png=s.resources->find(raw.dependencies.front());
 if(png==s.resources->end()||png->second.kind!=ResourceKind::Reference||!png->second.bytes||png->second.bytes->empty())return error(ErrorCode::CorruptData,"lineart.original-missing");
 return std::span<const std::uint8_t>(b).subspan(16);
}
}
