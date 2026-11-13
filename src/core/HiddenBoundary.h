#pragma once
#include "core/MyPaintCroppedRaster.h"
#include "core/LineartRaster.h"
#include <bit>
#include <cmath>
namespace drawborad::hiddenBoundary {
inline constexpr char Stage[]="drawborad.hidden.fill-boundary.v1";
inline constexpr char Magic[]="DBHBN001";
struct Point{double x,y;};
struct View{NodeId lineart;bool enabled;double radius;std::vector<Point> points;};
inline bool tagged(const Resource&r){return r.kind==ResourceKind::Reference&&r.bytes&&r.bytes->size()>=8&&std::equal(Magic,Magic+8,r.bytes->begin());}
inline Result<View> parse(const DocumentSnapshot&s,const LayerNode&n){
 if(n.stage!=Stage||n.kind!=NodeKind::Object||n.parent!=Document::rootId()||n.visible||!n.locked||n.opacity!=1||n.blend!=BlendMode::Normal||n.transform!=LayerTransform{}||!n.children.empty()||n.content.size()!=1||!n.maskId.empty()||!n.sourceResource.empty()||n.sourceKind!=SourceKind::Reconstructed)return error(ErrorCode::UnsupportedFeature,"boundary.node");
 if(!s.resources||!s.nodes||!s.resources->contains(n.content[0]))return error(ErrorCode::CorruptData,"boundary.resource");
 const auto&r=s.resources->at(n.content[0]);if(!tagged(r)||r.bytes->size()<69||r.bytes->size()>69+256*16||r.dependencies.size()!=1||r.id!=n.content[0]||sha256(*r.bytes)!=r.id)return error(ErrorCode::CorruptData,"boundary.resource-layout");
 const auto&b=*r.bytes;View v;std::copy_n(b.begin()+8,16,v.lineart.bytes.begin());v.enabled=b[56]!=0;v.radius=std::bit_cast<double>(croppedRaster::get(b,57,8));auto count=croppedRaster::get(b,65,4);
 if(b[56]>1||count<2||count>256||b.size()!=69+count*16||!std::isfinite(v.radius)||v.radius<.5||v.radius>4)return error(ErrorCode::InvalidInput,"boundary.layout");
 auto source=s.nodes->find(v.lineart);if(source==s.nodes->end()||source->second.stage!=LineartStage||source->second.parent!=Document::rootId()||source->second.transform!=LayerTransform{}||source->second.sourceResource!=r.dependencies[0]||!std::equal(r.dependencies[0].bytes.begin(),r.dependencies[0].bytes.end(),b.begin()+24))return error(ErrorCode::UnsupportedFeature,"boundary.lineart");
 auto sourcePixels=lineartRasterPixels(s,v.lineart);if(!sourcePixels)return sourcePixels.error();
 for(std::size_t i=0;i<count;i++){Point p{std::bit_cast<double>(croppedRaster::get(b,69+i*16,8)),std::bit_cast<double>(croppedRaster::get(b,77+i*16,8))};if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>=s.spec.pixelSize.width||p.y>=s.spec.pixelSize.height)return error(ErrorCode::InvalidInput,"boundary.point");v.points.push_back(p);}return v;
}
inline Resource resource(NodeId source,Hash sourceHash,const std::vector<Point>&points,double radius,bool enabled){
 // Size the fixed prefix before copying; repeated growth triggers GCC -O3 overflow diagnostics.
 Bytes b(57);
 std::copy_n(Magic,8,b.begin());
 std::copy(source.bytes.begin(),source.bytes.end(),b.begin()+8);
 std::copy(sourceHash.bytes.begin(),sourceHash.bytes.end(),b.begin()+24);
 b[56]=enabled?1:0;croppedRaster::put(b,std::bit_cast<std::uint64_t>(radius),8);croppedRaster::put(b,points.size(),4);for(const auto&p:points){croppedRaster::put(b,std::bit_cast<std::uint64_t>(p.x),8);croppedRaster::put(b,std::bit_cast<std::uint64_t>(p.y),8);}Resource r;r.kind=ResourceKind::Reference;r.id=sha256(b);r.dependencies={sourceHash};r.bytes=std::make_shared<const Bytes>(std::move(b));return r;
}
inline Result<Command> add(const DocumentSnapshot&s,NodeId id,NodeId source,const std::vector<Point>&points,double radius){
 if(!s.nodes||!s.resources||id.empty()||s.nodes->contains(id)||points.size()<2||points.size()>256)return error(ErrorCode::InvalidInput,"boundary.input");
 if(!s.nodes->contains(source))return error(ErrorCode::InvalidInput,"boundary.source");
 auto r=resource(source,s.nodes->at(source).sourceResource,points,radius,true);LayerNode n;n.id=id;n.parent=Document::rootId();n.kind=NodeKind::Object;n.name="Hidden fill boundary";n.stage=Stage;n.visible=false;n.locked=true;n.content={r.id};
 auto candidate=s;auto resources=std::make_shared<ResourceMap>(*s.resources);(*resources)[r.id]=r;candidate.resources=resources;auto valid=parse(candidate,n);if(!valid)return valid.error();Command c;c.operations={InstallResource{r},AddNode{n}};return c;
}
inline Result<Command> toggle(const DocumentSnapshot&s,NodeId id,bool enabled){
 if(!s.nodes||!s.nodes->contains(id))return error(ErrorCode::InvalidInput,"boundary.id");
 auto n=s.nodes->at(id);auto v=parse(s,n);if(!v)return v.error();auto r=resource(v.value().lineart,s.nodes->at(v.value().lineart).sourceResource,v.value().points,v.value().radius,enabled);n.content={r.id};const auto&siblings=s.nodes->at(Document::rootId()).children;auto it=std::find(siblings.begin(),siblings.end(),id);if(it==siblings.end())return error(ErrorCode::CorruptData,"boundary.parent");SetProperties unlock;unlock.id=id;unlock.locked=false;Command c;c.operations={InstallResource{r},unlock,RemoveNode{id},AddNode{n,std::size_t(it-siblings.begin())}};return c;
}
}
