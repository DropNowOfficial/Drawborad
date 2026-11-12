#include "ProjectCodec.h"
#include "Binary.h"
#include <algorithm>
#include <limits>
namespace drawborad {
namespace {
using binary::Reader;
using binary::Writer;
struct UnsupportedEngine {};
void put(Writer& w, const ColorValue& c) {
    w.hash(c.profileId);
    for (auto x : c.channels)
        w.real(x);
    w.u64(c.activeChannels);
    w.real(c.alpha);
}
ColorValue color(Reader& r) {
    ColorValue c;
    c.profileId = r.hash();
    for (auto& x : c.channels)
        x = r.real();
    auto n = r.u64();
    if (n > 4)
        throw binary::Invalid{};
    c.activeChannels = std::uint8_t(n);
    c.alpha = r.real();
    return c;
}
void put(Writer& w, const ColorSpec& c) {
    w.u64(std::uint64_t(c.format.mode));
    w.u64(std::uint64_t(c.format.depth));
    w.boolean(c.format.alpha);
    w.hash(c.profileId);
}
ColorSpec colorspec(Reader& r) {
    auto mode = r.u64(), depth = r.u64();
    if (mode > 1 || (depth != 8 && depth != 16))
        throw binary::Invalid{};
    ColorSpec c;
    c.format = {ColorMode(mode), Depth(depth), r.boolean()};
    c.profileId = r.hash();
    return c;
}
void put(Writer& w, const DocumentSpec& d) {
    w.u64(d.pixelSize.width); w.u64(d.pixelSize.height); w.real(d.ppi); put(w, d.colorSpec);
    w.boolean(d.background.transparent); put(w, d.background.paperColor);
    w.u64(d.estimatedBytes); w.u64(d.tileCount);
}
DocumentSpec spec(Reader& r) {
    DocumentSpec d; d.pixelSize={r.u64(),r.u64()}; d.ppi=r.real(); d.colorSpec=colorspec(r);
    d.background={r.boolean(),color(r)}; d.estimatedBytes=r.u64(); d.tileCount=r.u64(); return d;
}
void putIds(Writer& w, const std::vector<Id>& ids) { w.u64(ids.size()); for(auto x:ids) w.id(x); }
std::vector<Id> ids(Reader& r) { auto n=r.count(65536); std::vector<Id> v; v.reserve(n); while(n--) v.push_back(r.id()); return v; }
void putHashes(Writer& w,const std::vector<Hash>& hs){w.u64(hs.size());for(auto h:hs)w.hash(h);}
std::vector<Hash> hashes(Reader& r){auto n=r.count();std::vector<Hash> v;v.reserve(n);while(n--)v.push_back(r.hash());return v;}
void put(Writer& w,const LayerNode& n){
 w.id(n.id);w.id(n.parent);w.id(n.objectId);w.u64(std::uint64_t(n.kind));w.text(n.name);w.text(n.stage);w.hash(n.maskId);w.hash(n.sourceResource);putHashes(w,n.content);
 for(auto a:n.transform.affine){w.real(a);}w.real(n.opacity);w.u64(std::uint64_t(n.blend));w.boolean(n.visible);w.boolean(n.locked);w.u64(std::uint64_t(n.sourceKind));
 w.u64(std::bit_cast<std::uint64_t>(n.bounds.x));w.u64(std::bit_cast<std::uint64_t>(n.bounds.y));w.u64(n.bounds.width);w.u64(n.bounds.height);putIds(w,n.children);putIds(w,n.provenance);w.boolean(n.composite);
}
LayerNode node(Reader& r){
 LayerNode n;n.id=r.id();n.parent=r.id();n.objectId=r.id();auto k=r.u64();if(k>3)throw binary::Invalid{};n.kind=NodeKind(k);n.name=r.text();n.stage=r.text(256);n.maskId=r.hash();n.sourceResource=r.hash();n.content=hashes(r);
 for(auto& a:n.transform.affine){a=r.real();}n.opacity=r.real();auto b=r.u64();if(b>4)throw binary::Invalid{};n.blend=BlendMode(b);n.visible=r.boolean();n.locked=r.boolean();auto s=r.u64();if(s>2)throw binary::Invalid{};n.sourceKind=SourceKind(s);
 n.bounds={std::bit_cast<std::int64_t>(r.u64()),std::bit_cast<std::int64_t>(r.u64()),r.u64(),r.u64()};n.children=ids(r);n.provenance=ids(r);n.composite=r.boolean();return n;
}
void put(Writer& w,const Resource& a){w.hash(a.id);w.u64(std::uint64_t(a.kind));putHashes(w,a.dependencies);w.hash(sha256(*a.bytes));w.blob(*a.bytes);}
Resource resource(Reader& r){Resource a;a.id=r.hash();auto k=r.u64();if(k>4)throw binary::Invalid{};a.kind=ResourceKind(k);a.dependencies=hashes(r);auto digest=r.hash();a.bytes=std::make_shared<const Bytes>(r.blob());if(sha256(*a.bytes)!=digest)throw binary::Invalid{};return a;}
void put(Writer& w,const StrokeEvent& e){
 w.id(e.id);w.id(e.objectId);w.id(e.layerId);w.u64(e.startUs);w.u64(e.endUs);w.text(e.toolId);put(w,e.color);w.u64(e.geometry.size());
 for(auto& p:e.geometry){w.real(p.x);w.real(p.y);w.real(p.pressure);w.boolean(bool(p.tiltX));if(p.tiltX)w.real(*p.tiltX);w.boolean(bool(p.tiltY));if(p.tiltY)w.real(*p.tiltY);w.u64(p.timestampUs);w.u64(p.deviceKind);w.u64(p.capabilities);}
 w.u64(e.seed);w.u64(e.parentRevision);w.u64(e.engineVersion);
 if(e.engineVersion>=2){w.real(e.style.width);w.real(e.style.opacity);w.real(e.style.hardness);w.u64(std::uint64_t(e.style.shape));w.u64(e.style.angle);w.blob(e.style.grain);}
}
StrokeEvent event(Reader& r){
 StrokeEvent e;e.id=r.id();e.objectId=r.id();e.layerId=r.id();e.startUs=r.u64();e.endUs=r.u64();e.toolId=r.text(256);e.color=color(r);auto n=r.count();e.geometry.reserve(n);
 while(n--){InputSample p;p.x=r.real();p.y=r.real();p.pressure=r.real();if(r.boolean())p.tiltX=r.real();if(r.boolean())p.tiltY=r.real();p.timestampUs=r.u64();auto d=r.u64(),c=r.u64();if(d>UINT32_MAX||c>UINT32_MAX)throw binary::Invalid{};p.deviceKind=std::uint32_t(d);p.capabilities=std::uint32_t(c);e.geometry.push_back(p);}
 e.seed=r.u64();e.parentRevision=r.u64();auto v=r.u64();if(v!=1&&v!=2&&v!=8)throw UnsupportedEngine{};e.engineVersion=std::uint32_t(v);
 if(v>=2){e.style.width=r.real();e.style.opacity=r.real();e.style.hardness=r.real();auto shape=r.u64(),angle=r.u64();if(shape>1||angle>7)throw binary::Invalid{};e.style.shape=NibShape(shape);e.style.angle=std::uint8_t(angle);e.style.grain=r.blob(64);}return e;
}
}
Result<Bytes> encodeDelta(const DocumentSnapshot& before,const DocumentSnapshot& after){
 auto valid=validateSnapshot(after);if(!valid)return valid.error();if(!before.documentId.empty()&&(before.documentId!=after.documentId||after.revision<=before.revision))return error(ErrorCode::StaleRevision,"journal.order");
 Writer w;const bool bucket=std::any_of(after.eventIndex->begin(),after.eventIndex->end(),[](const auto& e){return e.engineVersion==8;});w.text(bucket?"DRAW-DELTA-8":"DRAW-DELTA-1");w.id(before.documentId);w.u64(before.revision);auto h=before.documentId.empty()?Result<Hash>{Hash{}}:stateHash(before);if(!h)return h.error();w.hash(h.value());w.id(after.documentId);w.u64(after.revision);put(w,after.spec);
 std::vector<Id> removed;std::vector<LayerNode> changed;for(auto&[id,n]:*before.nodes)if(!after.nodes->contains(id))removed.push_back(id);for(auto&[id,n]:*after.nodes){auto it=before.nodes->find(id);if(it==before.nodes->end()||it->second!=n)changed.push_back(n);}putIds(w,removed);w.u64(changed.size());for(auto& n:changed)put(w,n);
 std::vector<Hash> deleted;std::vector<Resource> resources;for(auto&[id,r]:*before.resources)if(!after.resources->contains(id))deleted.push_back(id);for(auto&[id,r]:*after.resources){auto it=before.resources->find(id);if(it==before.resources->end()||it->second!=r)resources.push_back(r);}putHashes(w,deleted);w.u64(resources.size());for(auto& r:resources)put(w,r);
 bool append=after.eventIndex->size()>=before.eventIndex->size()&&std::equal(before.eventIndex->begin(),before.eventIndex->end(),after.eventIndex->begin());auto start=append?before.eventIndex->size():0;w.boolean(append);w.u64(after.eventIndex->size()-start);for(auto i=start;i<after.eventIndex->size();i++)put(w,after.eventIndex->at(i));if(w.data.size()>MaxProjectBytes)return error(ErrorCode::BudgetExceeded,"project.record-limit");return w.data;
}
Result<DocumentSnapshot> decodeDelta(std::span<const std::uint8_t> bytes,const DocumentSnapshot& base){
 if(bytes.size()>MaxProjectBytes)return error(ErrorCode::BudgetExceeded,"project.record-limit");
 try{Reader r{bytes};const auto format=r.text(32);if(format!="DRAW-DELTA-1"&&format!="DRAW-DELTA-8"){if(format.starts_with("DRAW-DELTA-"))throw UnsupportedEngine{};throw binary::Invalid{};}auto prevId=r.id();auto prevRev=r.u64();auto prevHash=r.hash();if(prevId!=base.documentId||prevRev!=base.revision)return error(ErrorCode::StaleRevision,"journal.base");if(!base.documentId.empty()){auto h=stateHash(base);if(!h)return h.error();if(h.value()!=prevHash)return error(ErrorCode::DigestMismatch,"journal.base-hash");}
 DocumentSnapshot out;out.documentId=r.id();out.revision=r.u64();out.spec=spec(r);if(!base.documentId.empty()&&(out.documentId!=base.documentId||out.revision<=base.revision))throw binary::Invalid{};
 auto ns=std::make_shared<NodeMap>(*base.nodes);for(auto id:ids(r))ns->erase(id);auto count=r.count(65536);while(count--){auto n=node(r);(*ns)[n.id]=std::move(n);}out.nodes=ns;
 auto rs=std::make_shared<ResourceMap>(*base.resources);for(auto h:hashes(r))rs->erase(h);count=r.count();while(count--){auto a=resource(r);(*rs)[a.id]=std::move(a);}out.resources=rs;bool append=r.boolean();auto ev=append?std::make_shared<EventList>(*base.eventIndex):std::make_shared<EventList>();count=r.count(2000000);if(ev->size()+count>2000000)throw binary::Invalid{};while(count--)ev->push_back(event(r));out.eventIndex=ev;if(!r.done())throw binary::Invalid{};auto v=validateSnapshot(out);if(!v)return v.error();const bool bucket=std::any_of(out.eventIndex->begin(),out.eventIndex->end(),[](const auto& e){return e.engineVersion==8;});if(bucket!=(format=="DRAW-DELTA-8"))throw binary::Invalid{};return out;
 }catch(const UnsupportedEngine&){return error(ErrorCode::UnsupportedFeature,"project.engine-version");}catch(const binary::Invalid&){return error(ErrorCode::CorruptData,"project.decode");}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"project.decode-allocation");}
}
Result<Hash> stateHash(const DocumentSnapshot& s){auto canonical=s;canonical.revision=0;auto bytes=encodeDelta(DocumentSnapshot{},canonical);if(!bytes)return bytes.error();return sha256(bytes.value());}
Result<Bytes> frameRecord(std::span<const std::uint8_t> payload){if(payload.size()>MaxProjectBytes)return error(ErrorCode::BudgetExceeded,"journal.record-size");Writer w;w.u64(payload.size());w.hash(sha256(payload));w.raw(payload);return w.data;}
Result<Record> readRecord(std::span<const std::uint8_t> bytes,std::size_t offset){try{if(offset>bytes.size())throw binary::Invalid{};Reader r{bytes.subspan(offset)};auto n=r.u64();if(n>MaxProjectBytes||n>SIZE_MAX)throw binary::Invalid{};auto hash=r.hash();auto data=r.raw(std::size_t(n));if(sha256(data)!=hash)return error(ErrorCode::DigestMismatch,"journal.checksum");return Record{Bytes(data.begin(),data.end()),offset+r.pos};}catch(const binary::Invalid&){return error(ErrorCode::CorruptData,"journal.truncated");}}
}
