#include "core/PersistentRasterPaint.h"
#include "storage/ProjectStore.h"
#include "storage/Binary.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace drawborad;
namespace {
const auto layer=Id::fromNumber(25);
DocumentSnapshot fixture(const DocumentSnapshot& base,std::size_t cursor){
 auto s=base;auto rs=std::make_shared<ResourceMap>(*s.resources);auto ns=std::make_shared<NodeMap>(*s.nodes);
 auto patch=persistentRaster::makePatch(s,{3,5,4,3},Bytes(4*3*4,127));assert(patch);(*rs)[patch.value().id]=patch.value();
 persistentRaster::History history;history.records={{Id::fromNumber(101),patch.value().id,255},{Id::fromNumber(102),patch.value().id,128}};history.cursor=cursor;
 auto manifest=persistentRaster::manifest(history);assert(manifest);(*rs)[manifest.value().id]=manifest.value();
 LayerNode n;n.id=layer;n.parent=Document::rootId();n.stage=persistentRaster::Stage;n.sourceKind=SourceKind::ImportedRaster;n.sourceResource=manifest.value().id;n.name="Persistent paint";n.bounds={0,0,64,64};
 if(!ns->contains(layer))ns->at(Document::rootId()).children.push_back(layer);
 (*ns)[layer]=n;
 s.nodes=ns;s.resources=rs;++s.revision;assert(validateSnapshot(s));return s;
}
void roundtrip(const DocumentSnapshot&s,std::size_t cursor){auto bytes=encodeDelta({},s);assert(bytes);binary::Reader reader{bytes.value()};assert(reader.text(32)=="DRAW-DELTA-25");auto decoded=decodeDelta(bytes.value(),{});assert(decoded&&decoded.value().sameContent(s));auto h=persistentRaster::read(decoded.value(),layer);assert(h&&h.value().cursor==cursor&&h.value().records.size()==2);assert(stateHash(s).value()==stateHash(decoded.value()).value());}
}
int main(){
 NewDocumentRequest req;req.dimensions={64,64,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(99));assert(doc);const auto old=doc.value().snapshot();
 const auto full=fixture(old,2),zero=fixture(full,0),partial=fixture(zero,1);roundtrip(full,2);roundtrip(zero,0);roundtrip(partial,1);
 auto bytes=encodeDelta({},zero);assert(bytes);auto truncated=bytes.value();truncated.resize(truncated.size()-8);assert(!decodeDelta(truncated,{}));
 auto lower=bytes.value();std::string tag="DRAW-DELTA-25";auto pos=std::search(lower.begin(),lower.end(),tag.begin(),tag.end());assert(pos!=lower.end());*(pos+tag.size()-1)='4';auto bad=decodeDelta(lower,{});assert(!bad&&bad.error().messageKey=="project.format-feature-mismatch");
 auto delta=encodeDelta(full,zero);assert(delta);auto recovered=decodeDelta(delta.value(),full);assert(recovered&&recovered.value().sameContent(zero));
 // Corrupt only the withdrawn record's patch bytes. Cursor zero is not validation exemption.
 auto broken=zero;auto resources=std::make_shared<ResourceMap>(*zero.resources);auto hist=persistentRaster::read(zero,layer);assert(hist);auto& raw=resources->at(hist.value().records.back().patch);auto corrupt=std::make_shared<Bytes>(*raw.bytes);corrupt->back()^=1;raw.bytes=corrupt;broken.resources=resources;assert(!encodeDelta({},broken));
 const auto dir=std::filesystem::temp_directory_path()/("persistent-storage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));assert(std::filesystem::create_directory(dir));ProjectStore store;NativeFileIO io;
 const auto oldPath=dir/"old.draw",newPath=dir/"new25.draw";assert(store.save(oldPath,old));assert(store.save(newPath,full));const auto oldHash=io.hash(oldPath,MaxProjectBytes).value(),newHash=io.hash(newPath,MaxProjectBytes).value();
 assert(!store.save(oldPath,full));assert(!store.save(newPath,old));assert(!store.appendJournal(oldPath,old,full));assert(io.hash(oldPath,MaxProjectBytes).value()==oldHash&&io.hash(newPath,MaxProjectBytes).value()==newHash);
 assert(store.appendJournal(newPath,full,zero));auto reopened=store.recover(newPath);assert(reopened&&reopened.value().snapshot.sameContent(zero));assert(store.save(dir/"cursor-zero.draw",zero));assert(store.recover(dir/"cursor-zero.draw").value().snapshot.sameContent(zero));
 for(bool reverse:{false,true}){
  auto p=dir/(reverse?"reverse.draw":"forward.draw");const auto& primary=reverse?full:old;const auto& alien=reverse?old:full;assert(store.save(p,primary));auto original=io.hash(p,MaxProjectBytes).value();
  auto encoded=encodeDelta({},alien);assert(encoded);auto framed=frameRecord(encoded.value());assert(framed&&io.write(p.string()+".journal",framed.value(),true));auto fail=store.recover(p);assert(!fail&&fail.error().messageKey=="project.mixed-format-journal");assert(io.hash(p,MaxProjectBytes).value()==original);std::filesystem::remove(p.string()+".journal");
  auto other=dir/(reverse?"alien-old.draw":"alien-new.draw");assert(store.save(other,alien));auto contents=io.read(other,MaxProjectBytes);assert(contents&&io.write(p.string()+".bak",contents.value(),true));fail=store.recover(p);assert(!fail&&fail.error().messageKey=="project.mixed-format-backup");assert(io.hash(p,MaxProjectBytes).value()==original);std::filesystem::remove(p.string()+".bak");assert(store.recover(p).value().snapshot.sameContent(primary));
 }
 if(const char* path=std::getenv("DRAWBORAD_PERSISTENT_STORAGE_PROOF")){assert(store.save(path,zero));assert(store.save(std::string(path)+".active.draw",full));}
 std::filesystem::remove_all(dir);std::cout<<"PASS format25 persistent active/redo/zero cursor, exact hash roundtrip, lower header/truncation/corrupt redo refusal, journal and bidirectional generation protection\n";
}
