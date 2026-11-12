#include "core/LineartRaster.h"
#include "ui/LayerStream.h"
#include "codecs/OraStream.h"
#include "storage/ProjectCodec.h"
#include "storage/ProjectStore.h"
#include <random>
#include "storage/Binary.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv){
 NewDocumentRequest request;request.dimensions={11,17,Unit::In,300};request.transparent=false;request.paperColor.channels={1,1,1,0};
 auto spec=validateDocument(request,{});assert(spec);assert(spec.value().pixelSize==PixelSize(3300,5100));
 auto doc=Document::create(spec.value(),Id::fromNumber(91));assert(doc);auto original=doc.value().snapshot();
 assert(planMergedDocument(original));auto plain=encodeDelta({},original);assert(plain);binary::Reader plainHeader{plain.value()};assert(plainHeader.text(32)=="DRAW-DELTA-1");
 MergedBudget tiny;tiny.maxBytes=64*1024*1024;assert(!planMergedDocument(original,tiny));
 auto tooLarge=request;tooLarge.dimensions={8192,8192,Unit::Px,300};auto largeSpec=validateDocument(tooLarge,{});assert(largeSpec);auto largeDoc=Document::create(largeSpec.value(),Id::fromNumber(92));assert(largeDoc);assert(!planMergedDocument(largeDoc.value().snapshot()));
 TileData raw;raw.width=3300;raw.height=5100;raw.rowStride=13200;raw.bytes.resize(67320000,255);
 for(unsigned y=0;y<5100;y++){auto i=std::size_t(y)*13200+1650*4;raw.bytes[i]=raw.bytes[i+1]=raw.bytes[i+2]=0;i=std::size_t(y)*13200+1700*4;raw.bytes[i]=raw.bytes[i+1]=raw.bytes[i+2]=128;}
 auto png=encodePngBounded(raw,{},128*1024*1024);assert(png);
 auto line=makeLineartRasterCommand(std::move(raw.bytes),std::move(png.value()),3300,5100,Id::fromNumber(2));assert(line);assert(doc.value().apply(line.value(),0));
 auto source=doc.value().snapshot();assert(source.resources->size()==2);
 {auto bytes=encodeDelta({},source);assert(bytes);binary::Reader header{bytes.value()};assert(header.text(32)=="DRAW-DELTA-11");auto loaded=decodeDelta(bytes.value(),{});assert(loaded&&loaded.value().sameContent(source));}
 {
  const auto folder=std::filesystem::temp_directory_path()/("poster-storage-"+std::to_string(std::random_device{}()));assert(std::filesystem::create_directory(folder));
  ProjectStore store;const auto oldPath=folder/"old.draw",newPath=folder/"poster.draw";
  assert(store.save(oldPath,original));const auto oldSize=std::filesystem::file_size(oldPath);
  assert(!store.save(oldPath,source));assert(std::filesystem::file_size(oldPath)==oldSize);
  assert(store.save(newPath,source));const auto loaded=store.recover(newPath);assert(loaded&&loaded.value().snapshot.sameContent(source));
  NativeFileIO io;const auto before=io.hash(newPath,MaxProjectBytes);assert(before);
  assert(!store.save(newPath,original));assert(io.hash(newPath,MaxProjectBytes).value()==before.value());
  std::filesystem::copy_file(oldPath,newPath.string()+".bak");assert(!store.recover(newPath));
  std::filesystem::remove(newPath.string()+".bak");
  std::filesystem::copy_file(oldPath,newPath.string()+".journal");assert(!store.recover(newPath));
  assert(io.hash(newPath,MaxProjectBytes).value()==before.value());
  std::filesystem::remove_all(folder);
 }
 // Preserve pre-existing semantics for collections of <=64 MiB resources.
 {auto legacyCollection=original;auto items=std::make_shared<ResourceMap>();auto data=std::make_shared<const Bytes>(LegacyMaxResourceBytes,0);
  for(unsigned i=1;i<=5;i++){Resource r;r.kind=ResourceKind::Reference;r.bytes=data;Bytes seed{std::uint8_t(i+50)};r.id=sha256(seed);(*items)[r.id]=r;}
  legacyCollection.resources=items;assert(validateSnapshot(legacyCollection));
 }
 // References alias the same bytes to test accounting without four huge allocations.
 auto over=source;auto resources=std::make_shared<ResourceMap>(*source.resources);const auto big=std::find_if(resources->begin(),resources->end(),[](const auto& v){return v.second.bytes->size()>LegacyMaxResourceBytes;});assert(big!=resources->end());
 const auto baseResource=big->second;
 for(unsigned i=1;i<=3;i++){auto r=baseResource;Bytes seed{std::uint8_t(i)};r.id=sha256(seed);(*resources)[r.id]=r;}
 over.resources=resources;auto rejected=validateSnapshot(over);assert(!rejected&&rejected.error().code==ErrorCode::BudgetExceeded);
 LayerNode color;color.id=Id::fromNumber(3);color.parent=Document::rootId();color.name="Color";assert(doc.value().apply(Command(AddNode{color,0}),source.revision));
 StrokeEvent event;event.id=Id::fromNumber(4);event.layerId=color.id;event.engineVersion=8;event.toolId="bucket";event.parentRevision=doc.value().snapshot().revision;event.color.channels={0,1,1,0};
 for(unsigned y=0;y<5100;y++)event.geometry.insert(event.geometry.end(),{{0,double(y),1,{},{},0,0,0},{3300,double(y),1,{},{},0,0,0}});
 assert(doc.value().apply(Command(Paint{event,{0,0,3300,5100}}),event.parentRevision));auto s=doc.value().snapshot();
 auto merged=renderMergedDocument(s,{s.revision});assert(merged);assert(merged.value().merged.width==3300&&merged.value().merged.height==5100);
 for(unsigned y:{0u,511u,512u,5099u})for(unsigned x:{0u,511u,512u,1649u,1650u,1651u,1700u,3299u}){
  auto at=std::size_t(y)*13200+x*4;const auto value=x==1650?0:x==1700?128:255;
  assert(merged.value().merged.bytes[at]==0&&merged.value().merged.bytes[at+1]==value&&merged.value().merged.bytes[at+2]==value&&merged.value().merged.bytes[at+3]==255);
 }
 auto stream=NativeLayerStream::create(s,{s.revision});assert(stream&&stream.value().size()==3);
 if(argc>1){ExportPlan plan;plan.request.format=ExportFormat::Ora;plan.request.path=argv[1];plan.request.size={3300,5100};plan.request.overwrite=true;
  auto written=writeOraStream(plan,stream.value().size(),[&](std::size_t i,std::stop_token stop){return stream.value().read(i,stop);},merged.value().merged,{});assert(written);
 }
 std::cout<<"native poster 3300x5100, 70 tiles, raw resource v11, streamed 3-layer export passed\n";
}
