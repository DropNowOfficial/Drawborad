#pragma once
#include "core/Commands.h"
#include "core/Digest.h"
#include <algorithm>
#include <span>
namespace drawborad::croppedRaster {
inline constexpr char Stage[]="drawborad.mypaint.cropped.rgba.v1";
inline constexpr char NormalStage[]="drawborad.mypaint.cropped.normal.rgba.v1";
inline bool isStage(const std::string& stage){return stage==Stage || stage==NormalStage;}
inline constexpr std::size_t HeaderBytes=96;
struct Limits {std::uint64_t rawBytes=16*1024*1024,pngBytes=16*1024*1024,manifestBytes=1024*1024;};
struct View {Rect bounds;std::span<const std::uint8_t> rgba;Hash png,manifest;};
inline bool limitsValid(Limits l){return l.rawBytes>=HeaderBytes && l.rawBytes<=MaxResourceBytes && l.pngBytes>0 && l.pngBytes<=MaxResourceBytes && l.manifestBytes>0 && l.manifestBytes<=1024*1024;}
inline bool fits(Rect r,std::uint64_t cw,std::uint64_t ch){return r.x>=0 && r.y>=0 && r.width && r.height && cw<=8192 && ch<=8192 && cw*ch<=20000000 && std::uint64_t(r.x)<cw && std::uint64_t(r.y)<ch && r.width<=cw-std::uint64_t(r.x) && r.height<=ch-std::uint64_t(r.y);}
inline void put(Bytes& b,std::uint64_t n,unsigned count){for(unsigned i=0;i<count;++i)b.push_back(std::uint8_t(n>>(i*8)));}
inline std::uint64_t get(const Bytes& b,std::size_t p,unsigned count){std::uint64_t n=0;for(unsigned i=0;i<count;++i)n|=std::uint64_t(b[p+i])<<(i*8);return n;}
// PNG and manifest are immutable opaque source bytes here. Production entry must
// decode PNG, validate JSON and verify pixels correspond before this constructor.
inline Result<Command> makeImpl(Bytes rgba,Bytes png,Bytes manifest,Rect roi,std::uint64_t cw,std::uint64_t ch,NodeId id,Limits limits, bool normal) try {
 if(!limitsValid(limits)||!fits(roi,cw,ch)||id.empty())return error(ErrorCode::InvalidInput,"crop.layout");
 const auto pixels=roi.width*roi.height;
 if(pixels>(limits.rawBytes-HeaderBytes)/4 || rgba.size()!=pixels*4 || png.empty() || png.size()>limits.pngBytes || manifest.empty() || manifest.size()>limits.manifestBytes)return error(ErrorCode::BudgetExceeded,"crop.bytes");
 Resource p;p.kind=ResourceKind::Reference;p.id=sha256(png);p.bytes=std::make_shared<const Bytes>(std::move(png));
 Resource m;m.kind=ResourceKind::Reference;m.id=sha256(manifest);m.bytes=std::make_shared<const Bytes>(std::move(manifest));
 Bytes raw{'D','B','M','P','R','O','I',std::uint8_t(normal?'2':'1')};raw.reserve(HeaderBytes+rgba.size());put(raw,roi.width,4);put(raw,roi.height,4);put(raw,std::uint64_t(roi.x),8);put(raw,std::uint64_t(roi.y),8);
 raw.insert(raw.end(),p.id.bytes.begin(),p.id.bytes.end());raw.insert(raw.end(),m.id.bytes.begin(),m.id.bytes.end());raw.insert(raw.end(),rgba.begin(),rgba.end());
 Resource r;r.kind=ResourceKind::Reference;r.id=sha256(raw);r.bytes=std::make_shared<const Bytes>(std::move(raw));r.dependencies={p.id,m.id};
 LayerNode n;n.id=id;n.parent=Document::rootId();n.name=normal?"MyPaint paint (Normal raster)":"MyPaint ink (experimental raster)";n.stage=normal?NormalStage:Stage;n.sourceKind=SourceKind::ImportedRaster;n.sourceResource=r.id;n.bounds=roi;n.blend=normal?BlendMode::Normal:BlendMode::Multiply;n.locked=true;
 Command c;c.operations={InstallResource{p},InstallResource{m},InstallResource{r},AddNode{n}};return c;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"crop.allocation");}
inline Result<Command> make(Bytes rgba,Bytes png,Bytes manifest,Rect roi,std::uint64_t cw,std::uint64_t ch,NodeId id,Limits limits={}) {
 return makeImpl(std::move(rgba),std::move(png),std::move(manifest),roi,cw,ch,id,limits,false);
}
inline Result<Command> makeNormal(Bytes rgba,Bytes png,Bytes manifest,Rect roi,std::uint64_t cw,std::uint64_t ch,NodeId id,Limits limits={}) {
 return makeImpl(std::move(rgba),std::move(png),std::move(manifest),roi,cw,ch,id,limits,true);
}
inline Result<View> parse(const DocumentSnapshot& s,NodeId id,Limits limits={}){
 if(!s.nodes||!s.resources||!limitsValid(limits))return error(ErrorCode::CorruptData,"crop.structure");
 auto ni=s.nodes->find(id);if(ni==s.nodes->end())return error(ErrorCode::InvalidInput,"crop.node");const auto& n=ni->second;
 if(n.kind!=NodeKind::Layer||!isStage(n.stage)||n.sourceKind!=SourceKind::ImportedRaster||n.blend!=(n.stage==NormalStage?BlendMode::Normal:BlendMode::Multiply)||!n.content.empty()||!n.children.empty()||n.transform!=LayerTransform{}||!fits(n.bounds,s.spec.pixelSize.width,s.spec.pixelSize.height))return error(ErrorCode::UnsupportedFeature,"crop.node-layout");
 auto ri=s.resources->find(n.sourceResource);if(ri==s.resources->end())return error(ErrorCode::CorruptData,"crop.resource");const auto& r=ri->second;
 if(!r.bytes||r.kind!=ResourceKind::Reference||r.dependencies.size()!=2||r.bytes->size()<HeaderBytes||r.bytes->size()>limits.rawBytes)return error(ErrorCode::CorruptData,"crop.resource-layout");
 const auto& b=*r.bytes;
 const std::uint8_t magic[]={'D','B','M','P','R','O','I',std::uint8_t(n.stage==NormalStage?'2':'1')};
 if(!std::equal(std::begin(magic),std::end(magic),b.begin())||get(b,8,4)!=n.bounds.width||get(b,12,4)!=n.bounds.height||get(b,16,8)!=std::uint64_t(n.bounds.x)||get(b,24,8)!=std::uint64_t(n.bounds.y)||b.size()!=HeaderBytes+n.bounds.width*n.bounds.height*4)return error(ErrorCode::CorruptData,"crop.header");
 if(r.id!=n.sourceResource||sha256(b)!=r.id)return error(ErrorCode::DigestMismatch,"crop.digest");
 for(std::size_t i=0;i<2;++i){const auto& hash=r.dependencies[i];if(!std::equal(hash.bytes.begin(),hash.bytes.end(),b.begin()+32+i*32))return error(ErrorCode::CorruptData,"crop.binding");
  auto it=s.resources->find(hash);if(it==s.resources->end()||!it->second.bytes||it->second.kind!=ResourceKind::Reference||!it->second.dependencies.empty()||it->second.bytes->empty()||it->second.bytes->size()>(i?limits.manifestBytes:limits.pngBytes))return error(ErrorCode::CorruptData,"crop.source");
  if(it->second.id!=hash||sha256(*it->second.bytes)!=hash)return error(ErrorCode::DigestMismatch,"crop.source-digest");}
 return View{n.bounds,std::span<const std::uint8_t>(b).subspan(HeaderBytes),r.dependencies[0],r.dependencies[1]};
}
// Snapshot-bound immutable views; no external insertion or unchecked constructor.
class PreparedViews {
 std::shared_ptr<const NodeMap> nodes_;
 std::shared_ptr<const ResourceMap> resources_;
 DocumentSpec spec_;
 std::map<NodeId,View> views_;
 public:
 static Result<PreparedViews> prepare(const DocumentSnapshot& s){
  if(!s.nodes||!s.resources)return error(ErrorCode::CorruptData,"crop.structure");
  PreparedViews out;out.nodes_=s.nodes;out.resources_=s.resources;out.spec_=s.spec;
  for(const auto& [id,node]:*s.nodes)if(isStage(node.stage)){auto view=parse(s,id);if(!view)return view.error();out.views_.emplace(id,view.value());}
  return out;
 }
 const View* find(const DocumentSnapshot& s,NodeId id)const{
  if(s.nodes!=nodes_||s.resources!=resources_||s.spec!=spec_)return nullptr;
  auto it=views_.find(id);return it==views_.end()?nullptr:&it->second;
 }
};
inline Result<void> copyRegion(const View& view,Rect tile,std::span<std::uint8_t> output){
 if((view.bounds.x<0||view.bounds.y<0||!view.bounds.width||!view.bounds.height||view.bounds.width>8192||view.bounds.height>8192||view.bounds.x>8192||view.bounds.y>8192))return error(ErrorCode::InvalidInput,"crop.copy-bounds");
 if(tile.x<0||tile.y<0||tile.x>8192||tile.y>8192||tile.width>8192||tile.height>8192||output.size()!=tile.width*tile.height*4||view.rgba.size()!=view.bounds.width*view.bounds.height*4)return error(ErrorCode::InvalidInput,"crop.copy-layout");
 const auto x0=std::max(tile.x,view.bounds.x),y0=std::max(tile.y,view.bounds.y);
 const auto x1=std::min(tile.x+std::int64_t(tile.width),view.bounds.x+std::int64_t(view.bounds.width));
 const auto y1=std::min(tile.y+std::int64_t(tile.height),view.bounds.y+std::int64_t(view.bounds.height));
 if(x1<=x0||y1<=y0)return {};
 for(auto y=y0;y<y1;y++)std::copy_n(view.rgba.begin()+((y-view.bounds.y)*view.bounds.width+x0-view.bounds.x)*4,std::size_t(x1-x0)*4,output.begin()+((y-tile.y)*tile.width+x0-tile.x)*4);
 return {};
}

}
