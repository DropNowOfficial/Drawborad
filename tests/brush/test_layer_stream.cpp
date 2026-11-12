#include "core/LineartRaster.h"
#include "core/Digest.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
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
 auto stream=NativeLayerStream::create(filled,{filled.revision});assert(stream);assert(stream.value().size()==3);
 Bytes composed(4*4,0);
 for(std::size_t n=stream.value().size();n>0;--n){
  auto exported=stream.value().read(n-1);assert(exported);const auto& l=exported.value();
  if(n==1){assert(l.name=="Original lineart"&&l.blend==BlendMode::Multiply);assert(l.pixels.bytes==rgba);}
  if(!l.visible)continue;
  for(unsigned i=0;i<16;i+=4){
   const auto sa=(unsigned(l.pixels.bytes[i+3])*unsigned(std::lround(l.opacity*255))+127)/255;
   if(!sa)continue;
   const auto da=unsigned(composed[i+3]),den=sa*255+da*(255-sa);
   for(unsigned c=0;c<3;c++){
    const std::uint64_t cs=l.pixels.bytes[i+c],cb=composed[i+c];
    if(l.blend==BlendMode::Multiply){auto divisor=std::uint64_t(den)*255;composed[i+c]=std::uint8_t((cs*sa*(255-da)*255+cb*da*(255-sa)*255+cs*cb*sa*da+divisor/2)/divisor);}
    else composed[i+c]=std::uint8_t((cs*sa*255+cb*da*(255-sa)+den/2)/den);
   }
   composed[i+3]=std::uint8_t((den+127)/255);
  }
 }
 assert(composed==output.value().merged.bytes);
 assert(stream.value().statistics().indexedEvents==filled.eventIndex->size());
 assert(!stream.value().read(3));
 auto hidden=filled;auto hiddenNodes=std::make_shared<NodeMap>(*filled.nodes);hiddenNodes->at(Id::fromNumber(3)).visible=false;hiddenNodes->at(Id::fromNumber(3)).opacity=.35;hidden.nodes=hiddenNodes;
 auto hiddenStream=NativeLayerStream::create(hidden,{hidden.revision});assert(hiddenStream);auto hiddenLayer=hiddenStream.value().read(1);assert(hiddenLayer);
 assert(!hiddenLayer.value().visible&&hiddenLayer.value().opacity==.35&&hiddenLayer.value().pixels.bytes[3]==255);
 assert(*filled.resources->at(original.nodes->at(Id::fromNumber(2)).sourceResource).bytes==*original.resources->at(original.nodes->at(Id::fromNumber(2)).sourceResource).bytes);
 std::cout<<"PASS layer stream top-first/recomposite/hidden/opacity plus immutable raster lineart, source-over-white, under-color Multiply, general alpha, unsupported imported rejection\n";
}
