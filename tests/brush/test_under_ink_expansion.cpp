#include "brush/BucketFill.h"
#include "core/LineartRaster.h"
#include "ui/MergedDocument.h"
#include "codecs/RasterWriters.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace drawborad;

int main(){
 NewDocumentRequest req;req.dimensions={64,48,Unit::Px,300};req.transparent=false;req.paperColor.channels={1,1,1,0};req.paperColor.alpha=1;
 auto spec=validateDocument(req,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(50));assert(d);
 TileData raw;raw.width=64;raw.height=48;raw.rowStride=256;raw.format={ColorMode::RGB,Depth::U8,true};raw.bytes.resize(64*48*4,255);
 for(auto x0:{4u,36u})for(unsigned y=4;y<=40;y++)for(unsigned x=x0;x<=x0+24;x++){
  const auto depth=std::min({x-x0,x0+24-x,y-4,40-y});unsigned c=depth<2?0:depth==2?128:depth==3?220:255;
  for(unsigned channel=0;channel<3;channel++)raw.bytes[(y*64+x)*4+channel]=std::uint8_t(c);
 }
 raw.bytes[(20*64+7)*4+3]=128;
 for(unsigned c=0;c<4;c++)raw.bytes[(21*64+6)*4+c]=0;
 raw.bytes[(21*64+6)*4]=40;raw.bytes[(21*64+6)*4+1]=100;raw.bytes[(21*64+6)*4+2]=200; // Transparent black is NOT an eligible ink pixel.
 auto sourcePng=encodePngBounded(raw,{},1024*1024);assert(sourcePng);
 auto ink=makeLineartRasterCommand(raw.bytes,sourcePng.value(),64,48,Id::fromNumber(60));assert(ink);assert(d.value().apply(ink.value(),0));
 auto add=addFillLayerCommand(d.value().snapshot(),Id::fromNumber(61),true,true);assert(add);assert(d.value().apply(add.value(),d.value().snapshot().revision));auto source=d.value().snapshot();
 auto reference=renderMergedDocument(source,{source.revision});assert(reference);ColorValue color;color.channels={0,1,1,0};
 auto defaultCommand=bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(70),reference.value().merged,12,20,0,color);assert(defaultCommand);
 auto explicitOff=bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(70),reference.value().merged,12,20,0,color,{},0);assert(explicitOff);
 assert(std::get<Paint>(defaultCommand.value().operations[0]).event==std::get<Paint>(explicitOff.value().operations[0]).event);
 for(unsigned distance=0;distance<=2;distance++){
  auto copy=Document::fromSnapshot(source);assert(copy);auto command=bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(70),reference.value().merged,12,20,0,color,{},distance);assert(command);
  const auto& event=std::get<Paint>(command.value().operations[0]).event;assert(event.engineVersion==8&&event.toolId=="bucket");
  assert(copy.value().apply(command.value(),source.revision));auto painted=copy.value().snapshot();assert(*painted.resources==*source.resources);assert(*painted.nodes==*source.nodes);
  auto result=renderMergedDocument(painted,{painted.revision});assert(result);

  auto sample=[&](unsigned x,unsigned y,unsigned c){return result.value().merged.bytes[(y*64+x)*4+c];};
  assert(sample(12,20,0)==0&&sample(12,20,1)==255);
  assert(sample(7,19,0)==(distance?0:220));assert(sample(6,19,0)==(distance==2?0:128));
  assert(sample(7,20,0)==(distance?0:237));assert(sample(7,20,1)==237);
  assert(sample(6,21,0)==255&&sample(6,21,1)==255); // Transparent raw pixel cannot be added.
  for(unsigned y=0;y<48;y++)for(unsigned x=0;x<64;x++){
   const auto i=(y*64+x)*4;const bool interior=x>=8&&x<=24&&y>=8&&y<=36;
   bool changed=false;for(unsigned c=0;c<4;c++)changed|=result.value().merged.bytes[i+c]!=reference.value().merged.bytes[i+c];
   if(changed&&!interior){assert(x>=4&&x<=28&&y>=4&&y<=40);assert(raw.bytes[i+3]>0);assert(raw.bytes[i]!=255||raw.bytes[i+1]!=255||raw.bytes[i+2]!=255);}
   if(x>=32||x<4||y<4||y>40)for(unsigned c=0;c<4;c++)assert(result.value().merged.bytes[i+c]==reference.value().merged.bytes[i+c]);
  }
  assert(copy.value().undo());assert(copy.value().snapshot().sameContent(source));assert(copy.value().redo());assert(copy.value().snapshot().sameContent(painted));
 }
 assert(!bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,12,20,0,color,{},3));
 auto graySeed=bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,7,19,0,color,{},1);assert(!graySeed&&graySeed.error().messageKey=="bucket.under-ink-white-or-transparent-seed-required");
 assert(bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,7,19,0,color));
 auto rejected=[&](DocumentSnapshot altered){assert(!bucketFillCommand(altered,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,12,20,0,color,{},1));};
 auto altered=source;auto nodes=std::make_shared<NodeMap>(*source.nodes);altered.nodes=nodes;nodes->at(Id::fromNumber(60)).locked=false;rejected(altered);
 *nodes=*source.nodes;nodes->at(Id::fromNumber(60)).visible=false;rejected(altered);
 *nodes=*source.nodes;nodes->at(Id::fromNumber(60)).opacity=.5;rejected(altered);
 *nodes=*source.nodes;std::reverse(nodes->at(Document::rootId()).children.begin(),nodes->at(Document::rootId()).children.end());rejected(altered);
 auto colored=d.value().snapshot();auto changedResources=std::make_shared<ResourceMap>(*colored.resources);auto changedNodes=std::make_shared<NodeMap>(*colored.nodes);colored.resources=changedResources;colored.nodes=changedNodes;
 const auto originalRaw=changedNodes->at(Id::fromNumber(60)).sourceResource;auto resource=changedResources->at(originalRaw);auto data=std::make_shared<Bytes>(*resource.bytes);(*data)[16+(4*64+4)*4+1]=1;resource.bytes=data;resource.id=sha256(*data);changedResources->erase(originalRaw);(*changedResources)[resource.id]=resource;changedNodes->at(Id::fromNumber(60)).sourceResource=resource.id;
 auto coloredDenied=bucketFillCommand(colored,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,12,20,0,color,{},1);assert(!coloredDenied&&coloredDenied.error().messageKey=="bucket.under-ink-grayscale-required");
 assert(!bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,64,0,0,color,{},1));
 assert(!bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,0,48,0,color,{},2));
 assert(!bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,12,20,0,color,{},UINT32_MAX));
 auto ambiguous=source;auto ambiguousNodes=std::make_shared<NodeMap>(*source.nodes);ambiguous.nodes=ambiguousNodes;LayerNode extra;extra.id=Id::fromNumber(90);extra.parent=Document::rootId();(*ambiguousNodes)[extra.id]=extra;ambiguousNodes->at(Document::rootId()).children.push_back(extra.id);rejected(ambiguous);
 std::stop_source stop;stop.request_stop();assert(!bucketFillCommand(source,Id::fromNumber(61),Id::fromNumber(71),reference.value().merged,12,20,0,color,stop.get_token(),1));
 std::cout<<"PASS real under-ink0/1/2: antialiased+partial-alpha edge, no white spill, second region unchanged, source bytes immutable, default identical, undo/redo and unsupported stack rejected\n";
}
