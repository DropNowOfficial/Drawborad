#include "core/MyPaintCroppedRaster.h"
#include "storage/ProjectStore.h"
#include "storage/Binary.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <random>
using namespace drawborad;
namespace fs=std::filesystem;
Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
void write(const fs::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());assert(f.good());}
int main(int argc,char** argv){
 NewDocumentRequest req;req.dimensions={3300,5100,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(22));assert(d);auto legacy=d.value().snapshot();
 auto cmd=croppedRaster::make(Bytes(8*6*4,0),{137,80,78,71},{'{','}'},{510,509,8,6},3300,5100,Id::fromNumber(30));assert(cmd);assert(d.value().apply(cmd.value(),0));auto cropped=d.value().snapshot();
 auto encoded=encodeDelta({},cropped);assert(encoded);binary::Reader r{encoded.value()};assert(r.text(32)=="DRAW-DELTA-13");auto decoded=decodeDelta(encoded.value(),{});assert(decoded&&decoded.value().sameContent(cropped));
 auto mixed=cropped;auto events=std::make_shared<EventList>();StrokeEvent e;e.id=Id::fromNumber(90);e.engineVersion=12;e.toolId="dry-ink";e.geometry={{1,1,1,{},{},0,0,0}};events->push_back(e);mixed.eventIndex=events;
 auto mix=encodeDelta({},mixed);assert(mix);binary::Reader mr{mix.value()};assert(mr.text(32)=="DRAW-DELTA-13");assert(decodeDelta(mix.value(),{}));
 auto disguised=encoded.value();disguised[8+12]='2'; // replace final ASCII header digit13 ->12
 auto bad=decodeDelta(disguised,{});assert(!bad&&bad.error().code==ErrorCode::UnsupportedFeature);
 auto dir=fs::temp_directory_path()/("crop-integration-"+std::to_string(std::random_device{}()));fs::create_directory(dir);ProjectStore store;auto path=dir/"crop.draw";
 assert(store.save(path,cropped));auto original=read(path);assert(!store.save(path,legacy));assert(read(path)==original);
 auto oldPath=dir/"old.draw";assert(store.save(oldPath,legacy));auto oldBytes=read(oldPath);assert(!store.save(oldPath,cropped));assert(read(oldPath)==oldBytes);
 write(path.string()+".bak",oldBytes);assert(!store.recover(path));fs::remove(path.string()+".bak");
 write(path.string()+".journal",oldBytes);assert(!store.recover(path));fs::remove(path.string()+".journal");
 auto next=cropped;next.revision++;assert(store.appendJournal(path,cropped,next));assert(store.recover(path));assert(store.save(path,next));assert(store.recover(path));
 auto old12=legacy;old12.eventIndex=events;auto p12=dir/"old12.draw";assert(store.save(p12,old12));auto bytes12=read(p12);
 assert(!store.save(p12,cropped));assert(read(p12)==bytes12);assert(!store.save(path,old12));
 auto next12=old12;next12.revision=next.revision+1;assert(!store.appendJournal(path,next,next12));
 write(path.string()+".bak",bytes12);assert(!store.recover(path));fs::remove(path.string()+".bak");
 write(path.string()+".journal",bytes12);assert(!store.recover(path));fs::remove(path.string()+".journal");
 auto paintOnRaster=mixed;auto badEvents=std::make_shared<EventList>(*events);badEvents->front().layerId=Id::fromNumber(30);paintOnRaster.eventIndex=badEvents;assert(!encodeDelta({},paintOnRaster));
 auto wrong=cropped;auto nodes=std::make_shared<NodeMap>(*cropped.nodes);nodes->at(Id::fromNumber(30)).bounds.x++;wrong.nodes=nodes;assert(!encodeDelta({},wrong));
 if(argc==2){write(argv[1],original);write(std::string(argv[1])+".bak",oldBytes);}
 fs::remove_all(dir);std::cout<<"PASS isolated format13, mixed12 priority, strict stage validation, cross-generation save/bak/journal guards\n";
}
