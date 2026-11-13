// Technical closed-region fixture, NOT the user's artwork or an artistic repair.
#include "core/Commands.h"
#include "core/Digest.h"
#include "brush/BucketFill.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv){
 const auto out=argc==2?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/
   ("drawborad-under-ink-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 assert(std::filesystem::create_directory(out));
 NewDocumentRequest req;req.dimensions={512,512,Unit::Px,300};
 auto spec=validateDocument(req,{});assert(spec);spec.value().background.transparent=false;
 spec.value().background.paperColor.channels={1,1,1,0};
 auto doc=Document::create(spec.value(),Id::fromNumber(100));assert(doc);
 const auto ink=Id::fromNumber(101),colorLayer=Id::fromNumber(102);
 LayerNode inkNode;inkNode.id=ink;inkNode.parent=Document::rootId();inkNode.name="Technical closed ink regions";
 assert(doc.value().apply(Command(AddNode{inkNode}),doc.value().snapshot().revision));
 auto draw=[&](unsigned id,std::initializer_list<std::pair<double,double>> points){
   StrokeEvent e;e.id=Id::fromNumber(id);e.layerId=ink;e.engineVersion=2;e.toolId="fineliner";
   e.style.width=3;e.style.hardness=1;e.color.channels={0,0,0,0};e.parentRevision=doc.value().snapshot().revision;
   for(const auto& [x,y]:points)e.geometry.push_back({x,y,1,{},{},0,0,0});
   assert(doc.value().apply(Command(Paint{e,{38,38,435,415}}),e.parentRevision));
 };
 draw(110,{{40,40},{470,40},{470,450},{40,450},{40,40}});
 draw(111,{{255,40},{255,450}});
 auto original=doc.value().snapshot();const auto originalEvents=*original.eventIndex;
 auto render=[&](const DocumentSnapshot& s,const char* name){
  auto r=renderMergedDocument(s,{s.revision});assert(r);std::ofstream f(out/name,std::ios::binary);assert(f);
  f.write(reinterpret_cast<const char*>(r.value().merged.bytes.data()),r.value().merged.bytes.size());assert(f.good());return r.value().merged.bytes;
 };
 ReferenceRenderer renderer;
 auto rawInk=renderer.renderRegion(original,ink,{0,0,512,512},{original.revision});assert(rawInk);
 auto a=render(original,"before.rgba");
 assert(ProjectStore{}.save(out/"lineart-source.draw",original));
 auto add=addFillLayerCommand(original,colorLayer,true,true);assert(add);
 assert(doc.value().apply(add.value(),original.revision));auto added=doc.value().snapshot();
 assert(added.nodes->at(Document::rootId()).children.front()==colorLayer);
 assert(added.nodes->at(Document::rootId()).children.back()==ink);
 auto ref=renderMergedDocument(added,{added.revision});assert(ref);
 ColorValue color;color.channels={.15,.65,.95,0};
 auto fill=bucketFillCommand(added,colorLayer,Id::fromNumber(112),ref.value().merged,120,200,0,color);assert(fill);
 assert(doc.value().apply(fill.value(),added.revision));auto filled=doc.value().snapshot();
 assert(filled.eventIndex->size()==originalEvents.size()+1);
 for(std::size_t i=0;i<originalEvents.size();++i)assert(filled.eventIndex->at(i)==originalEvents[i]);
 auto inkAfter=renderer.renderRegion(filled,ink,{0,0,512,512},{filled.revision});assert(inkAfter);
 assert(rawInk.value().bytes==inkAfter.value().bytes);
 auto b=render(filled,"after.rgba");std::size_t changed=0;
 for(unsigned y=0;y<512;y++)for(unsigned x=0;x<512;x++){
  auto pos=(y*512+x)*4;bool differs=false;
  for(unsigned c=0;c<4;c++)differs|=a[pos+c]!=b[pos+c];
  if(differs){++changed;assert(x>40&&x<255&&y>40&&y<450);}
  // Every existing dark/antialiased line pixel is untouched in the composite too.
  if(a[pos]!=255||a[pos+1]!=255||a[pos+2]!=255)
   for(unsigned c=0;c<4;c++)assert(a[pos+c]==b[pos+c]);
 }
 assert(changed>50000);assert(b[(200*512+120)*4]!=255);
 auto copy=out/"under-ink-copy.draw";assert(ProjectStore{}.save(copy,filled));
 assert(doc.value().undo());auto u=render(doc.value().snapshot(),"undo.rgba");assert(u==a);
 assert(doc.value().redo());assert(doc.value().snapshot().sameContent(filled));
 auto recovered=ProjectStore{}.recover(copy);assert(recovered);
 auto r=render(recovered.value().snapshot,"reopened.rgba");assert(r==b);
 std::ofstream json(out/"receipt.json");
 json<<"{\"fixture\":\"technical two closed regions, NOT user artwork\",\"width\":512,\"height\":512,\"format\":\"RGBA8\","
 <<"\"color_layer_below_ink\":true,\"sample_merged_layers\":true,\"changed_pixels\":"<<changed<<","
 <<"\"other_region_and_outside_unchanged\":true,\"ink_events_and_pixels_unchanged\":true,\"undo_equals_before\":true,\"reopened_equals_after\":true,"
 <<"\"before_sha256\":\""<<sha256(a).hex()<<"\",\"after_sha256\":\""<<sha256(b).hex()<<"\","
 <<"\"line_layer_before_sha256\":\""<<sha256(rawInk.value().bytes).hex()<<"\",\"line_layer_after_sha256\":\""<<sha256(inkAfter.value().bytes).hex()<<"\","
 <<"\"limitation\":\"Tolerance 0 preserves AA edge pixels; no halo removal or boundary expansion claimed\"}\n";
 assert(json.good());std::cout<<"PASS under-ink closed-region fill; changed="<<changed<<" output="<<out<<"\n";
}
