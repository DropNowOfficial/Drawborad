// Native event-only region tracing ingress. No source bitmap is accepted here.
#include "core/Commands.h"
#include "core/Digest.h"
#include "storage/ProjectStore.h"
#include "ui/LayerStream.h"
#include "codecs/OraStream.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace drawborad;
int main(int argc,char**argv){
 assert(argc==2);const std::filesystem::path out=argv[1];std::ifstream f(out/"plan.txt");assert(f);
 std::string magic;unsigned w,h,nlayers,events;f>>magic>>w>>h>>nlayers>>events;
 assert(magic=="COVERREGIONS1"&&w==1024&&h==1536&&nlayers<=16&&events<=256);
 NewDocumentRequest req;req.dimensions={double(w),double(h),Unit::Px,300};req.transparent=false;req.paperColor.channels={246./255,239./255,225./255,0};
 auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(0xc0fe));assert(doc);
 Command command;
 for(unsigned i=0;i<nlayers;i++){std::string name;f>>name;for(auto& c:name)if(c=='_')c=' ';LayerNode node;node.id=Id::fromNumber(10+i);node.parent=Document::rootId();node.name=name;node.bounds={0,0,w,h};command.operations.emplace_back(AddNode{node});}
 std::size_t points=0;
 for(unsigned i=0;i<events;i++){
  unsigned layer,r,g,b,runs;f>>layer>>r>>g>>b>>runs;assert(f&&layer<nlayers&&runs<=65536&&r<=255&&g<=255&&b<=255);points+=runs*2;assert(points<=1000000);
  StrokeEvent e;e.id=Id::fromNumber(1000+i);e.layerId=Id::fromNumber(10+layer);e.engineVersion=8;e.toolId="bucket";e.startUs=i;e.endUs=i;e.parentRevision=0;e.color.channels={r/255.,g/255.,b/255.,0};
  for(unsigned j=0;j<runs;j++){unsigned y,x0,x1;f>>y>>x0>>x1;assert(f&&y<h&&x0<x1&&x1<=w);e.geometry.push_back({double(x0),double(y),1,{},{},i,0,0});e.geometry.push_back({double(x1),double(y),1,{},{},i,0,0});}
  command.operations.emplace_back(Paint{e,{0,0,w,h}});
 }
 assert(doc.value().apply(command,0));const auto snapshot=doc.value().snapshot();assert(snapshot.resources->empty());
 auto plan=planMergedDocument(snapshot);if(!plan){std::cerr<<plan.error().messageKey<<"\n";return 2;}
 auto render=renderMergedDocument(snapshot,{snapshot.revision});if(!render){std::cerr<<render.error().messageKey<<"\n";return 3;}
 assert(ProjectStore{}.save(out/"cover_candidate.draw",snapshot));auto reopened=ProjectStore{}.recover(out/"cover_candidate.draw");assert(reopened&&snapshot.sameContent(reopened.value().snapshot));
 auto rerender=renderMergedDocument(reopened.value().snapshot,{snapshot.revision});assert(rerender&&rerender.value().merged.bytes==render.value().merged.bytes);
 RasterEncodingOptions options;options.ppi=300;auto png=encodePngBounded(render.value().merged,options,64*1024*1024);assert(png);
 std::ofstream image(out/"cover_candidate.png",std::ios::binary);image.write(reinterpret_cast<const char*>(png.value().data()),png.value().size());assert(image.good());
 auto stream=NativeLayerStream::create(snapshot,{snapshot.revision});assert(stream);ExportPlan ora;ora.request.format=ExportFormat::Ora;ora.request.path=out/"cover_candidate.ora";ora.request.size={w,h};
 auto exported=writeOraStream(ora,stream.value().size(),[&](std::size_t n,std::stop_token stop){return stream.value().read(n,stop);},render.value().merged,options);assert(exported);
 std::ofstream receipt(out/"native-receipt.json");receipt<<"{\"width\":1024,\"height\":1536,\"native_layers\":"<<nlayers<<",\"events\":"<<events<<",\"geometry_points\":"<<points<<",\"embedded_image_resources\":0,\"saved_reopened_pixels_equal\":true,\"render_sha256\":\""<<sha256(render.value().merged.bytes).hex()<<"\",\"method\":\"Reference-derived native color-region trace, NOT a hand redrawing or high-resolution restoration\"}\n";assert(receipt.good());
 std::cout<<"PASS native full-cover region candidate: "<<events<<" events, "<<points<<" points, "<<stream.value().size()<<" ORA layers\n";
}
