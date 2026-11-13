#pragma once
#include "core/MyPaintCroppedRaster.h"
namespace drawborad::cropMask {
// Format15 rectangular coverage mask. Original raster bytes are never changed.
inline constexpr char Magic[]="DBMSK001";
struct View {Rect rect;bool enabled;};
inline bool tagged(const Resource& r){return r.kind==ResourceKind::Mask && r.bytes && r.bytes->size()>=8 && std::equal(Magic,Magic+8,r.bytes->begin());}
inline Result<View> parse(const DocumentSnapshot& s,const Resource& r){
 if(!tagged(r)||r.bytes->size()!=41||!r.dependencies.empty()||(*r.bytes)[40]>1)return error(ErrorCode::CorruptData,"stroke-mask.layout");
 if(sha256(*r.bytes)!=r.id)return error(ErrorCode::DigestMismatch,"stroke-mask.digest");
 const auto& b=*r.bytes;const auto x=croppedRaster::get(b,8,8),y=croppedRaster::get(b,16,8),w=croppedRaster::get(b,24,8),h=croppedRaster::get(b,32,8);
 if(x>8192||y>8192)return error(ErrorCode::InvalidInput,"stroke-mask.bounds");
 Rect rect{std::int64_t(x),std::int64_t(y),w,h};
 if(!croppedRaster::fits(rect,s.spec.pixelSize.width,s.spec.pixelSize.height))return error(ErrorCode::InvalidInput,"stroke-mask.bounds");
 return View{rect,b[40]!=0};
}
inline bool supported(const DocumentSnapshot& s,NodeId id){
 if(!s.nodes||!s.resources)return false;
 auto it=s.nodes->find(id);if(it==s.nodes->end()||!croppedRaster::isStage(it->second.stage))return false;
 return it->second.maskId.empty() || (s.resources->contains(it->second.maskId)&&tagged(s.resources->at(it->second.maskId)));
}
inline Result<Command> set(const DocumentSnapshot& s,NodeId id,Rect rect,bool enabled){
 if(!supported(s,id)||!croppedRaster::fits(rect,s.spec.pixelSize.width,s.spec.pixelSize.height))return error(ErrorCode::InvalidInput,"stroke-mask.target-or-bounds");
 auto source=croppedRaster::parse(s,id);if(!source)return source.error();
 Bytes b(Magic,Magic+8);croppedRaster::put(b,std::uint64_t(rect.x),8);croppedRaster::put(b,std::uint64_t(rect.y),8);croppedRaster::put(b,rect.width,8);croppedRaster::put(b,rect.height,8);b.push_back(enabled?1:0);
 Resource r;r.kind=ResourceKind::Mask;r.id=sha256(b);r.bytes=std::make_shared<const Bytes>(std::move(b));
 Command c;c.operations.push_back(InstallResource{r});const bool locked=s.nodes->at(id).locked;
 if(locked){SetProperties p;p.id=id;p.locked=false;c.operations.push_back(p);}
 c.operations.push_back(SetMask{id,r.id});
 if(locked){SetProperties p;p.id=id;p.locked=true;c.operations.push_back(p);}
 return c;
}
inline Result<Command> toggle(const DocumentSnapshot& s,NodeId id,bool enabled){
 if(!supported(s,id)||s.nodes->at(id).maskId.empty())return error(ErrorCode::InvalidInput,"stroke-mask.missing");
 auto v=parse(s,s.resources->at(s.nodes->at(id).maskId));if(!v)return v.error();return set(s,id,v.value().rect,enabled);
}
inline unsigned coverage(const View& v,std::int64_t x,std::int64_t y){return !v.enabled || (x>=v.rect.x&&y>=v.rect.y&&std::uint64_t(x-v.rect.x)<v.rect.width&&std::uint64_t(y-v.rect.y)<v.rect.height)?255:0;}
}
