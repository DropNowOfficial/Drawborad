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
 assert(argc==2);const std::filesystem::path out=argv[1];std::ifstream f(out/"eye-plan.txt");assert(f);
 unsigned events;f>>events;assert(f&&events<512);const unsigned w=1600,h=1000,nlayers=4;
 NewDocumentRequest req;req.dimensions={double(w),double(h),Unit::Px,300};req.transparent=false;req.paperColor.channels={246./255,239./255,225./255,0};
 auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(0xfa51));assert(doc);
 Command command;const char* names[]={"Directional hair masses","Raised brow and eye contour","Pupil and highlights","Face and tension marks"};
 for(unsigned i=0;i<nlayers;i++){LayerNode node;node.id=Id::fromNumber(10+i);node.parent=Document::rootId();node.name=names[i];node.bounds={0,0,w,h};command.operations.emplace_back(AddNode{node});}
 std::size_t points=0;std::uint64_t tick=0;
 for(unsigned i=0;i<events;i++){
  unsigned layer,gray,count;double width;f>>layer>>width>>gray>>count;assert(f&&layer<4&&gray<=255&&count<=4096);points+=count;
  StrokeEvent e;e.id=Id::fromNumber(1000+i);e.layerId=Id::fromNumber(10+layer);e.engineVersion=width<=8?10:2;e.toolId=width<=8?"fineliner-aa":"marker";e.startUs=tick;e.endUs=tick+count-1;e.parentRevision=0;e.color.channels={gray/255.,gray/255.,gray/255.,0};e.style.width=width;e.style.hardness=1;
  for(unsigned j=0;j<count;j++){double x,y,pressure;f>>x>>y>>pressure;assert(f&&x>=0&&y>=0&&x<w&&y<h);e.geometry.push_back({x,y,pressure,{},{},tick++,0,0});}
  command.operations.emplace_back(Paint{e,{0,0,w,h}});
 }
 assert(doc.value().apply(command,0));const auto snapshot=doc.value().snapshot();assert(snapshot.resources->empty());
 auto plan=planMergedDocument(snapshot);if(!plan){std::cerr<<plan.error().messageKey<<"\n";return 2;}
 auto render=renderMergedDocument(snapshot,{snapshot.revision});if(!render){std::cerr<<render.error().messageKey<<"\n";return 3;}
 assert(ProjectStore{}.save(out/"eye_study.draw",snapshot));auto reopened=ProjectStore{}.recover(out/"eye_study.draw");assert(reopened&&snapshot.sameContent(reopened.value().snapshot));
 auto rerender=renderMergedDocument(reopened.value().snapshot,{snapshot.revision});assert(rerender&&rerender.value().merged.bytes==render.value().merged.bytes);
 RasterEncodingOptions options;options.ppi=300;auto png=encodePngBounded(render.value().merged,options,64*1024*1024);assert(png);
 std::ofstream image(out/"eye_study.png",std::ios::binary);image.write(reinterpret_cast<const char*>(png.value().data()),png.value().size());assert(image.good());
 auto stream=NativeLayerStream::create(snapshot,{snapshot.revision});assert(stream);ExportPlan ora;ora.request.format=ExportFormat::Ora;ora.request.path=out/"eye_study.ora";ora.request.size={w,h};
 auto exported=writeOraStream(ora,stream.value().size(),[&](std::size_t n,std::stop_token stop){return stream.value().read(n,stop);},render.value().merged,options);assert(exported);
 std::ofstream receipt(out/"native-receipt.json");receipt<<"{\"width\":1600,\"height\":1000,\"native_layers\":"<<nlayers<<",\"events\":"<<events<<",\"geometry_points\":"<<points<<",\"embedded_image_resources\":0,\"saved_reopened_pixels_equal\":true,\"render_sha256\":\""<<sha256(render.value().merged.bytes).hex()<<"\",\"method\":\"Manually authored Bezier eye study, NOT finished artwork\"}\n";assert(receipt.good());
 std::cout<<"PASS native manually authored eye study: "<<events<<" events, "<<points<<" points, "<<stream.value().size()<<" ORA layers\n";
}
