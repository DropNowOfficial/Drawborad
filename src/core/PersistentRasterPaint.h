#pragma once
#include "core/Document.h"
#include "core/Digest.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <span>
#include <stop_token>
namespace drawborad::persistentRaster {
inline constexpr char Stage[]="drawborad.paint.patch-history.rgba.v1";
inline constexpr std::uint64_t MaxRetainedBytes=128ull*1024*1024;
inline constexpr std::size_t MaxRecords=4096,MaxManifestBytes=1024*1024;
inline constexpr std::uint64_t MaxIndexEntries=262144,MaxPixelVisits=128ull*1024*1024;
inline constexpr unsigned IndexSide=256;
struct Record {Id id;Hash patch;std::uint8_t opacity=255;bool operator==(const Record&)const=default;};
struct History {std::vector<Record> records;std::size_t cursor{};};
struct Patch {Rect bounds;std::span<const std::uint8_t> rgba;};
inline bool present(const DocumentSnapshot&s){return s.nodes&&std::any_of(s.nodes->begin(),s.nodes->end(),[](const auto&p){return p.second.stage==Stage;});}
inline void put(Bytes&b,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(std::uint8_t(v>>(8*i)));}
inline std::uint64_t get(std::span<const std::uint8_t>b,std::size_t p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(b[p+i])<<(8*i);return v;}
inline bool fits(Rect r,const DocumentSnapshot&s){const auto sz=s.spec.pixelSize;return r.x>=0&&r.y>=0&&r.width&&r.height&&sz.width<=8192&&sz.height<=8192&&sz.width*sz.height<=20000000&&std::uint64_t(r.x)<sz.width&&std::uint64_t(r.y)<sz.height&&r.width<=sz.width-std::uint64_t(r.x)&&r.height<=sz.height-std::uint64_t(r.y);}
inline Result<Hash> digest(std::span<const std::uint8_t>b,std::stop_token stop){Sha256State hash;for(std::size_t p=0;p<b.size();p+=65536){if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");hash.update(b.subspan(p,std::min<std::size_t>(65536,b.size()-p)));}return hash.finish();}
inline Result<Patch> patch(const DocumentSnapshot&s,Hash h,std::stop_token stop={}){
 auto i=s.resources->find(h);if(i==s.resources->end()||!i->second.bytes)return error(ErrorCode::CorruptData,"paint-history.patch-missing");const auto&r=i->second;const auto&b=*r.bytes;
 const std::uint8_t magic[]={'D','B','P','A','T','C','H',1};
 if(r.kind!=ResourceKind::Reference||b.size()<32||b.size()>4*1024*1024+96||!std::equal(std::begin(magic),std::end(magic),b.begin())||r.id!=h||r.dependencies.size()>2)return error(ErrorCode::CorruptData,"paint-history.patch");
 auto hash=digest(b,stop);if(!hash)return hash.error();if(hash.value()!=h)return error(ErrorCode::DigestMismatch,"paint-history.patch-digest");
 Rect roi{std::int64_t(get(b,8,4)),std::int64_t(get(b,12,4)),get(b,16,4),get(b,20,4)};
 const auto deps=get(b,24,8);if(deps!=r.dependencies.size()||deps>2)return error(ErrorCode::CorruptData,"paint-history.patch-dependencies");const auto header=32+std::size_t(deps)*32;
 if(!fits(roi,s)||roi.width>2048||roi.height>2048||roi.width*roi.height>1048576||b.size()!=header+roi.width*roi.height*4)return error(ErrorCode::CorruptData,"paint-history.patch-layout");
 for(std::size_t j=0;j<deps;++j){if(!std::equal(r.dependencies[j].bytes.begin(),r.dependencies[j].bytes.end(),b.begin()+32+j*32)||!s.resources->contains(r.dependencies[j]))return error(ErrorCode::CorruptData,"paint-history.patch-provenance");}
 return Patch{roi,std::span<const std::uint8_t>(b).subspan(header)};
}
inline Result<Resource> makePatch(const DocumentSnapshot&s,Rect roi,std::span<const std::uint8_t>rgba,std::vector<Hash> provenance={})try{
 if(!fits(roi,s)||roi.width>2048||roi.height>2048||roi.width*roi.height>1048576||rgba.size()!=roi.width*roi.height*4||provenance.size()>2)return error(ErrorCode::InvalidInput,"paint-history.patch-input");
 Bytes b{'D','B','P','A','T','C','H',1};b.reserve(32+32*provenance.size()+rgba.size());put(b,std::uint64_t(roi.x),4);put(b,std::uint64_t(roi.y),4);put(b,roi.width,4);put(b,roi.height,4);put(b,provenance.size(),8);for(const auto&h:provenance){if(h.empty())return error(ErrorCode::InvalidInput,"paint-history.provenance-id");b.insert(b.end(),h.bytes.begin(),h.bytes.end());}b.insert(b.end(),rgba.begin(),rgba.end());Resource r;r.kind=ResourceKind::Reference;r.id=sha256(b);r.bytes=std::make_shared<const Bytes>(std::move(b));r.dependencies=std::move(provenance);return r;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"paint-history.allocation");}
inline Result<Resource> manifest(const History&h)try{
 if(h.records.size()>MaxRecords||h.cursor>h.records.size())return error(ErrorCode::InvalidInput,"paint-history.cursor");
 Bytes b{'D','B','P','H','I','S','T',1};put(b,h.records.size(),4);put(b,h.cursor,4);std::set<Id> ids;std::set<Hash> hashes;
 for(const auto&r:h.records){if(r.id.empty()||r.patch.empty()||!r.opacity||!ids.insert(r.id).second)return error(ErrorCode::InvalidInput,"paint-history.record");b.insert(b.end(),r.id.bytes.begin(),r.id.bytes.end());b.insert(b.end(),r.patch.bytes.begin(),r.patch.bytes.end());b.push_back(r.opacity);hashes.insert(r.patch);}
 Resource out;out.kind=ResourceKind::Reference;out.id=sha256(b);out.bytes=std::make_shared<const Bytes>(std::move(b));out.dependencies.assign(hashes.begin(),hashes.end());return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"paint-history.allocation");}
inline Result<History> read(const DocumentSnapshot&s,NodeId id,std::map<Hash,Patch>* shared=nullptr,std::stop_token stop={})try{
 std::map<Hash,Patch> local;auto& cached=shared?*shared:local;
 auto n=s.nodes->find(id);if(n==s.nodes->end())return error(ErrorCode::InvalidInput,"paint-history.layer");const auto&v=n->second;
 if(v.kind!=NodeKind::Layer||v.stage!=Stage||v.sourceKind!=SourceKind::ImportedRaster||!v.content.empty()||!v.children.empty()||!v.objectId.empty()||!v.maskId.empty()||v.transform!=LayerTransform{}||v.blend!=BlendMode::Normal||v.bounds!=Rect{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height})return error(ErrorCode::UnsupportedFeature,"paint-history.layer-layout");
 auto i=s.resources->find(v.sourceResource);if(i==s.resources->end()||!i->second.bytes)return error(ErrorCode::CorruptData,"paint-history.manifest-missing");const auto&r=i->second;const auto&b=*r.bytes;const std::uint8_t magic[]={'D','B','P','H','I','S','T',1};
 if(r.kind!=ResourceKind::Reference||b.size()<16||b.size()>MaxManifestBytes||!std::equal(std::begin(magic),std::end(magic),b.begin())||r.id!=v.sourceResource)return error(ErrorCode::CorruptData,"paint-history.manifest");
 auto hash=digest(b,stop);if(!hash)return hash.error();if(hash.value()!=r.id)return error(ErrorCode::DigestMismatch,"paint-history.manifest-digest");
 const auto count=get(b,8,4),cursor=get(b,12,4);if(count>MaxRecords||cursor>count||b.size()!=16+count*49)return error(ErrorCode::CorruptData,"paint-history.count");
 History out;out.cursor=std::size_t(cursor);out.records.reserve(std::size_t(count));std::set<Id> ids;std::set<Hash> hashes;
 for(std::size_t j=0;j<count;++j){const auto p=16+j*49;Record rec;std::copy_n(b.begin()+p,16,rec.id.bytes.begin());std::copy_n(b.begin()+p+16,32,rec.patch.bytes.begin());rec.opacity=b[p+48];if(rec.id.empty()||!rec.opacity||!ids.insert(rec.id).second)return error(ErrorCode::CorruptData,"paint-history.record");if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");if(!cached.contains(rec.patch)){auto pixels=patch(s,rec.patch,stop);if(!pixels)return pixels.error();cached.emplace(rec.patch,pixels.value());}hashes.insert(rec.patch);out.records.push_back(rec);}
 if(std::vector<Hash>(hashes.begin(),hashes.end())!=r.dependencies)return error(ErrorCode::CorruptData,"paint-history.dependencies");
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"paint-history.allocation");}
// Prepared immutable byte views; no PNG decoding or worker execution in rendering.
struct PreparedLayer {History history;std::vector<Patch> patches;std::map<std::pair<unsigned,unsigned>,std::vector<std::size_t>> index;};
class Prepared {
 DocumentId document_;DocumentSpec spec_;std::shared_ptr<const NodeMap> nodes_;std::shared_ptr<const ResourceMap> resources_;std::map<NodeId,PreparedLayer> layers_;std::uint64_t entries_{},records_{},work_{},hashBytes_{};
 public:
 std::uint64_t indexEntries()const{return entries_;}
 std::uint64_t estimatedBytes()const{return entries_*128+records_*192+layers_.size()*256;}
 std::uint64_t work()const{return work_+hashBytes_;}
 std::uint64_t uniquePatchCount()const{return uniquePatches_;}
 private:std::size_t uniquePatches_{};
 public:
 static Result<Prepared> prepare(const DocumentSnapshot&s,std::stop_token stop={})try{
  Prepared out;out.document_=s.documentId;out.spec_=s.spec;out.nodes_=s.nodes;out.resources_=s.resources;std::uint64_t entries=0,work=0;std::map<Hash,Patch> cache;
  for(const auto&[id,n]:*s.nodes)if(n.stage==Stage){auto h=read(s,id,&cache,stop);if(!h)return h.error();PreparedLayer l;l.history=std::move(h.value());if(l.history.records.size()>MaxRecords-out.records_)return error(ErrorCode::BudgetExceeded,"paint-history.document-records");out.records_+=l.history.records.size();
   for(std::size_t j=0;j<l.history.records.size();++j){if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");const auto p=cache.at(l.history.records[j].patch);l.patches.push_back(p);if(j>=l.history.cursor)continue;const auto r=p.bounds;
    if(r.width*r.height>MaxPixelVisits-work)return error(ErrorCode::BudgetExceeded,"paint-history.pixel-work");
    work+=r.width*r.height;
    for(unsigned y=unsigned(r.y)/IndexSide;y<=(unsigned(r.y)+unsigned(r.height)-1)/IndexSide;++y)for(unsigned x=unsigned(r.x)/IndexSide;x<=(unsigned(r.x)+unsigned(r.width)-1)/IndexSide;++x){if(++entries>MaxIndexEntries)return error(ErrorCode::BudgetExceeded,"paint-history.index-work");l.index[{x,y}].push_back(j);}
   }out.layers_.emplace(id,std::move(l));
  }out.entries_=entries;out.work_=work;out.uniquePatches_=cache.size();for(const auto&[hash,p]:cache){(void)hash;out.hashBytes_+=p.rgba.size()+96;}return out;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"paint-history.allocation");}
 Result<void> render(const DocumentSnapshot&s,NodeId id,Rect rect,std::span<std::uint8_t>output,std::stop_token stop={})const try{
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");
  if(s.documentId!=document_||s.spec!=spec_||s.nodes!=nodes_||s.resources!=resources_||!fits(rect,s)||rect.width>512||rect.height>512||output.size()!=rect.width*rect.height*4)return error(ErrorCode::InvalidInput,"paint-history.render-layout");
  auto it=layers_.find(id);if(it==layers_.end())return error(ErrorCode::InvalidInput,"paint-history.render-layer");const auto&l=it->second;std::set<std::size_t> candidates;
  for(unsigned y=unsigned(rect.y)/IndexSide;y<=(unsigned(rect.y)+unsigned(rect.height)-1)/IndexSide;++y)for(unsigned x=unsigned(rect.x)/IndexSide;x<=(unsigned(rect.x)+unsigned(rect.width)-1)/IndexSide;++x){auto ids=l.index.find({x,y});if(ids!=l.index.end())candidates.insert(ids->second.begin(),ids->second.end());}
  std::fill(output.begin(),output.end(),0);
  for(auto j:candidates){if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");const auto&p=l.patches[j];const auto&r=p.bounds;const auto x0=std::max(rect.x,r.x),y0=std::max(rect.y,r.y),x1=std::min(rect.x+std::int64_t(rect.width),r.x+std::int64_t(r.width)),y1=std::min(rect.y+std::int64_t(rect.height),r.y+std::int64_t(r.height));
   for(auto y=y0;y<y1;++y){if(stop.stop_requested())return error(ErrorCode::Cancelled,"paint-history.cancelled");for(auto x=x0;x<x1;++x){auto from=std::size_t(((y-r.y)*r.width+x-r.x)*4),to=std::size_t(((y-rect.y)*rect.width+x-rect.x)*4);unsigned sa=(unsigned(p.rgba[from+3])*l.history.records[j].opacity+127)/255;if(!sa)continue;unsigned da=output[to+3],den=sa*255+da*(255-sa);for(unsigned c=0;c<3;++c)output[to+c]=std::uint8_t((unsigned(p.rgba[from+c])*sa*255+unsigned(output[to+c])*da*(255-sa)+den/2)/den);output[to+3]=std::uint8_t((den+127)/255);}}
  }return {};
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"paint-history.allocation");}
};
} // namespace drawborad::persistentRaster
