#include "core/MyPaintCroppedRaster.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include "codecs/OraStream.h"
#include "codecs/RasterWriters.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace drawborad;
Bytes read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char**argv){assert(argc==2);std::filesystem::path d=argv[1];NewDocumentRequest r;r.dimensions={520,520,Unit::Px,300};r.transparent=false;r.paperColor.channels={0,0,0,0};r.paperColor.alpha=1;auto spec=validateDocument(r,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(8));assert(doc);auto blank=doc.value().snapshot();
 Bytes rgba={255,255,255,255,255,255,255,128,255,255,255,0,255,255,255,255};TileData tile;tile.width=2;tile.height=2;tile.rowStride=8;tile.format={ColorMode::RGB,Depth::U8,true};tile.bytes=rgba;auto png=encodePngBounded(tile,{},4096);assert(png);
 auto old=croppedRaster::make(rgba,png.value(),{'o'},{511,511,2,2},520,520,Id::fromNumber(30));assert(old);assert(doc.value().apply(old.value(),0));auto v13=doc.value().snapshot();
 auto cmd=croppedRaster::makeNormal(rgba,png.value(),{'n'},{511,511,2,2},520,520,Id::fromNumber(31));assert(cmd);assert(doc.value().apply(cmd.value(),v13.revision));auto v14=doc.value().snapshot();assert(croppedRaster::parse(v14,Id::fromNumber(31)));
 auto im=renderMergedDocument(v14,{v14.revision});assert(im);auto at=[&](unsigned x,unsigned y){return im.value().merged.bytes[(y*520+x)*4];};assert(at(511,511)==255&&at(512,511)==128&&at(511,512)==0&&at(512,512)==255);
 auto views=croppedRaster::PreparedViews::prepare(v14);assert(views);assert(!views.value().find(v13,Id::fromNumber(31)));auto invalid=v14;auto nodes=std::make_shared<NodeMap>(*v14.nodes);nodes->at(Id::fromNumber(31)).blend=BlendMode::Multiply;invalid.nodes=nodes;assert(!validateSnapshot(invalid));nodes->at(Id::fromNumber(31)).blend=BlendMode::Normal;nodes->at(Id::fromNumber(31)).stage=croppedRaster::Stage;assert(!validateSnapshot(invalid));
 nodes->at(Id::fromNumber(31)).stage=croppedRaster::NormalStage;
 nodes->at(Id::fromNumber(31)).sourceResource=v13.nodes->at(Id::fromNumber(30)).sourceResource;
 assert(!validateSnapshot(invalid)); // v1 raw cannot be relabelled Normal v2.
 assert(doc.value().undo());assert(doc.value().snapshot().sameContent(v13));assert(doc.value().redo());
 ProjectStore store;auto p13=d/"old.draw",p14=d/"normal.draw";assert(store.save(p13,v13));auto oldBytes=read(p13);assert(!store.save(p13,v14));assert(read(p13)==oldBytes);assert(store.save(p14,v14));auto bytes=read(p14);const std::string header="DRAW-DELTA-14";assert(std::search(bytes.begin(),bytes.end(),header.begin(),header.end())!=bytes.end());
 assert(!store.save(p14,v13));assert(read(p14)==bytes);assert(!store.appendJournal(p14,v13,v14));auto recovered=store.recover(p14);assert(recovered&&recovered.value().snapshot.sameContent(v14));
 std::filesystem::copy_file(p13,p14.string()+".bak");assert(!store.recover(p14));assert(read(p14)==bytes);std::filesystem::remove(p14.string()+".bak");
 auto changedDoc=Document::fromSnapshot(v14);assert(changedDoc);SetProperties properties;properties.id=Id::fromNumber(31);properties.opacity=.5;
 assert(!changedDoc.value().apply(Command(properties),v14.revision));SetProperties unlock;unlock.id=properties.id;unlock.locked=false;Command opacityCommand;opacityCommand.operations={unlock,properties};assert(changedDoc.value().apply(opacityCommand,v14.revision));auto next=changedDoc.value().snapshot();assert(store.appendJournal(p14,v14,next));assert(store.recover(p14));
 auto half=renderMergedDocument(next,{next.revision});assert(half&&half.value().merged.bytes[(511*520+511)*4]==128);

 auto coded=encodeDelta(DocumentSnapshot{},v14);assert(coded);auto fake=coded.value();std::string a="DRAW-DELTA-14",b="DRAW-DELTA-13";auto it=std::search(fake.begin(),fake.end(),a.begin(),a.end());assert(it!=fake.end());std::copy(b.begin(),b.end(),it);assert(!decodeDelta(fake,DocumentSnapshot{}));
 auto stream=NativeLayerStream::create(v14,{v14.revision});assert(stream);auto first=stream.value().read(0);assert(first&&first.value().blend==BlendMode::Normal&&first.value().pixels.bytes==rgba);
 ExportPlan plan;plan.request.path=d/"normal.ora";plan.request.format=ExportFormat::Ora;plan.request.size={520,520};auto out=writeOraStream(plan,stream.value().size(),[&](std::size_t i,std::stop_token stop){return stream.value().read(i,stop);},im.value().merged,{});assert(out);
 std::cout<<"PASS normal over black, alpha/transparent, cross512, strict stage/blend, undo/redo, floor14, cross-version overwrite/journal denial, decode mismatch, ORA source layer\n";
}
