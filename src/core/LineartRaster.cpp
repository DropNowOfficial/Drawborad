#include "LineartRaster.h"
#include "Digest.h"
#include <algorithm>
namespace drawborad {
namespace {
constexpr std::uint8_t magic[]={'D','B','L','R','G','B','A','1'};
void put(Bytes& b,std::uint32_t n){for(unsigned i=0;i<4;i++)b.push_back(std::uint8_t(n>>(i*8)));}
}
Result<Command> makeLineartRasterCommand(Bytes rgba,Bytes originalPng,std::uint32_t w,std::uint32_t h,NodeId id){
 const auto n=std::uint64_t(w)*h;
 if(!w||!h||n>(MaxResourceBytes-16)/4||rgba.size()!=n*4||id.empty()||originalPng.empty()||originalPng.size()>MaxResourceBytes)
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

}
