#include "core/MyPaintCroppedRaster.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include "codecs/OraStream.h"
#include "codecs/RasterWriters.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(int argc,char** argv){assert(argc==2);NewDocumentRequest req;req.dimensions={3300,5100,Unit::Px,300};req.paperColor.channels={.2,.4,.8,0};req.paperColor.alpha=1;req.transparent=false;
 auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(1));assert(doc);
 TileData local;local.width=8;local.height=6;local.rowStride=32;local.format={ColorMode::RGB,Depth::U8,true};local.bytes.resize(192);
 for(std::size_t i=0;i<48;i++){local.bytes[i*4]=128;local.bytes[i*4+1]=64;local.bytes[i*4+2]=200;local.bytes[i*4+3]=i==0?0:128;}
 auto png=encodePngBounded(local,{},4096);assert(png);auto cmd=croppedRaster::make(local.bytes,png.value(),{'{','}'},{510,509,8,6},3300,5100,Id::fromNumber(30));assert(cmd);
 std::get<AddNode>(cmd.value().operations.back()).node.opacity=.5;assert(doc.value().apply(cmd.value(),0));auto s=doc.value().snapshot();
 auto merged=renderMergedDocument(s,{s.revision});if(!merged){std::cerr<<merged.error().messageKey<<'\n';return 1;}const unsigned bg[3]={51,102,204},fg[3]={128,64,200};
 for(unsigned y=0;y<5100;y++)for(unsigned x=0;x<3300;x++)for(unsigned c=0;c<4;c++){
  const bool in=x>=510&&x<518&&y>=509&&y<515&&!(x==510&&y==509);unsigned expected=c==3?255:bg[c];
  if(in&&c<3){const std::uint64_t sa=64,da=255,den=sa*255+da*(255-sa),num=bg[c]*da*(255-sa)*255+std::uint64_t(fg[c])*bg[c]*sa*da;expected=unsigned((num+den*255/2)/(den*255));}
  assert(merged.value().merged.bytes[(std::size_t(y)*3300+x)*4+c]==expected);
 }
 auto stream=NativeLayerStream::create(s,{s.revision});assert(stream&&stream.value().size()==2);
 auto layer=stream.value().read(0);assert(layer);assert(layer.value().blend==BlendMode::Multiply&&layer.value().opacity==.5);
 for(unsigned y=0;y<5100;y++)for(unsigned x=0;x<3300;x++)for(unsigned c=0;c<4;c++){
  unsigned expected=0;if(x>=510&&x<518&&y>=509&&y<515)expected=local.bytes[((y-509)*8+x-510)*4+c];
  assert(layer.value().pixels.bytes[(std::size_t(y)*3300+x)*4+c]==expected);
 }
 layer.value().pixels.bytes.clear();layer.value().pixels.bytes.shrink_to_fit();
 ExportPlan plan;plan.request.format=ExportFormat::Ora;plan.request.path=argv[1];plan.request.size={3300,5100};plan.request.overwrite=true;
 auto out=writeOraStream(plan,stream.value().size(),[&](std::size_t i,std::stop_token stop){return stream.value().read(i,stop);},merged.value().merged,{});assert(out);
 auto hidden=s;auto nodes=std::make_shared<NodeMap>(*s.nodes);nodes->at(Id::fromNumber(30)).visible=false;hidden.nodes=nodes;
 auto blank=renderMergedDocument(hidden,{hidden.revision});assert(blank);assert(blank.value().merged.bytes[(510*3300+512)*4]==51);
 auto transparent=s;transparent.spec.background.transparent=true;auto clear=renderMergedDocument(transparent,{transparent.revision});assert(clear);assert(clear.value().merged.bytes[3]==0);auto pos=(510*3300+512)*4;assert(clear.value().merged.bytes[pos]==128&&clear.value().merged.bytes[pos+3]==64);
 auto prepared=croppedRaster::PreparedViews::prepare(s);assert(prepared);assert(prepared.value().find(s,Id::fromNumber(30)));
 assert(!prepared.value().find(hidden,Id::fromNumber(30)));assert(!prepared.value().find(transparent,Id::fromNumber(30)));
 auto corrupt=s;auto badResources=std::make_shared<ResourceMap>(*s.resources);corrupt.resources=badResources;
 const auto raw=s.nodes->at(Id::fromNumber(30)).sourceResource;auto badBytes=std::make_shared<Bytes>(*badResources->at(raw).bytes);badBytes->back()^=1;badResources->at(raw).bytes=badBytes;
 assert(!prepared.value().find(corrupt,Id::fromNumber(30)));assert(!renderMergedDocument(corrupt,{corrupt.revision}));
 Bytes large(1024*1024*4,0);for(std::size_t i=3;i<large.size();i+=4)large[i]=255;
 auto largeCommand=croppedRaster::make(std::move(large),{1},{2},{1000,2000,1024,1024},3300,5100,Id::fromNumber(31));assert(largeCommand);assert(doc.value().apply(largeCommand.value(),s.revision));
 auto largeState=doc.value().snapshot();assert(planMergedDocument(largeState));auto largeRender=renderMergedDocument(largeState,{largeState.revision});assert(largeRender);
 assert(largeRender.value().merged.bytes[(2000ull*3300+1000)*4]==0);assert(largeRender.value().merged.bytes[(3024ull*3300+2024)*4]==51);
 std::cout<<"PASS prepared views: cross-snapshot rejection, corrupted resource rejection, actual1Mi-pixel ROI on poster within unchanged work budget\n";
 std::cout<<"PASS real merged + LayerStream + PNG/ORA: native3300x5100 ROI crosses512, Multiply/opacity, hidden and transparent canvas\n";
}
