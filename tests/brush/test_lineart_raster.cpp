#include "core/LineartRaster.h"
#include "core/Digest.h"
#include "ui/MergedDocument.h"
#include "brush/BucketFill.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest req;req.dimensions={4,1,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);
 spec.value().background.transparent=false;spec.value().background.paperColor.channels={1,1,1,0};
 auto doc=Document::create(spec.value(),Id::fromNumber(1));assert(doc);
 const Bytes rgba={0,0,0,255,128,128,128,128,255,255,255,255,90,20,200,0};
 const Bytes png={137,80,78,71,13,10,26,10}; // Helper preserves bytes; PNG decode belongs to Session.
 auto import=makeLineartRasterCommand(rgba,png,4,1,Id::fromNumber(2));assert(import);
 assert(doc.value().apply(import.value(),0));auto original=doc.value().snapshot();
 auto pixels=lineartRasterPixels(original,Id::fromNumber(2));assert(pixels);
 assert(Bytes(pixels.value().begin(),pixels.value().end())==rgba);
 assert(original.nodes->at(Id::fromNumber(2)).locked);
 auto white=renderMergedDocument(original,{original.revision});assert(white);
 for(unsigned x=0;x<4;x++)for(unsigned c=0;c<3;c++)assert(white.value().merged.bytes[x*4+c]==(unsigned(rgba[x*4+c])*rgba[x*4+3]+255*(255-rgba[x*4+3])+127)/255);
 auto add=addFillLayerCommand(original,Id::fromNumber(3),true,true);assert(add);
 assert(doc.value().apply(add.value(),original.revision));auto added=doc.value().snapshot();
 ColorValue color;color.channels={64./255,128./255,192./255,0};
 auto ref=renderMergedDocument(added,{added.revision});assert(ref);
 auto fill=bucketFillCommand(added,Id::fromNumber(3),Id::fromNumber(4),ref.value().merged,0,0,255,color);assert(fill);
 assert(!bucketFillCommand(added,Id::fromNumber(2),Id::fromNumber(5),ref.value().merged,0,0,0,color));
 assert(doc.value().apply(fill.value(),added.revision));auto filled=doc.value().snapshot();
 auto output=renderMergedDocument(filled,{filled.revision});assert(output);
 const unsigned base[]={64,128,192};
 for(unsigned x=0;x<4;x++)for(unsigned c=0;c<3;c++){
  double cs=rgba[x*4+c]/255.,as=rgba[x*4+3]/255.,cb=base[c]/255.;
  assert(output.value().merged.bytes[x*4+c]==std::lround((cb*(1-as)+cs*cb*as)*255));
 }
 for(const auto& [id,r]:*original.resources)assert(*filled.resources->at(id).bytes==*r.bytes);
 // General alpha: partially transparent bottom, not just white-paper special case.
 auto transparent=filled;transparent.spec.background.transparent=true;
 auto events=std::make_shared<EventList>(*filled.eventIndex);events->back().color.alpha=128./255;transparent.eventIndex=events;
 auto mixed=renderMergedDocument(transparent,{transparent.revision});assert(mixed);
 for(unsigned x=0;x<4;x++)for(unsigned c=0;c<3;c++){
  double as=rgba[x*4+3]/255.,ab=128./255,cs=rgba[x*4+c]/255.,cb=base[c]/255.;
  double ao=as+ab*(1-as),co=(cs*as*(1-ab)+cb*ab*(1-as)+cs*cb*as*ab)/ao;
  assert(mixed.value().merged.bytes[x*4+c]==std::lround(co*255));
  assert(mixed.value().merged.bytes[x*4+3]==std::lround(ao*255));
 }
 auto invalid=original;auto nodes=std::make_shared<NodeMap>(*original.nodes);nodes->at(Id::fromNumber(2)).stage="unrecognized";invalid.nodes=nodes;
 assert(!renderMergedDocument(invalid,{invalid.revision}));
 assert(!makeLineartRasterCommand(rgba,png,3,1,Id::fromNumber(6)));
 std::cout<<"PASS immutable raster lineart, source-over-white, under-color Multiply, general alpha, unsupported imported rejection\n";
}
