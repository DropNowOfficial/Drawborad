#include "brush/OriginalLineartFill.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace drawborad;
void poster(unsigned w,unsigned h,bool save){
 NewDocumentRequest req;req.dimensions={double(w),double(h),Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);
 auto made=Document::create(spec.value(),Id::fromNumber(21));assert(made);auto& doc=made.value();
 Bytes raw(std::size_t(w)*h*4,255);
 // Entire native-resolution interior is a closed region, not an upscaled fixture.
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)if(!x||!y||x==w-1||y==h-1){auto i=(std::size_t(y)*w+x)*4;raw[i]=raw[i+1]=raw[i+2]=0;}
 auto ink=Id::fromNumber(22),fill=Id::fromNumber(23);
 auto add=makeLineartRasterCommand(std::move(raw),{'s','y','n','t','h'},w,h,ink);assert(add&&doc.apply(add.value(),0));
 auto layer=addFillLayerCommand(doc.snapshot(),fill,true,true);assert(layer&&doc.apply(layer.value(),doc.snapshot().revision));
 const auto before=doc.snapshot();ColorValue red;red.channels={1,0,0,0};red.alpha=1;
 auto command=originalLineartFillCommand(before,ink,fill,Id::fromNumber(24),w/2,h/2,red);assert(command);
 const auto& points=std::get<Paint>(command.value().operations[0]).event.geometry;assert(points.size()==2*(h-2));
 std::uint64_t area=0;for(std::size_t i=0;i<points.size();i+=2){assert(points[i].x==1&&points[i+1].x==w-1&&points[i].y==i/2+1);area+=points[i+1].x-points[i].x;}assert(area==std::uint64_t(w-2)*(h-2));
 assert(doc.apply(command.value(),before.revision));assert(doc.snapshot().spec.pixelSize==before.spec.pixelSize);
 for(const auto&[id,r]:*before.resources){assert(doc.snapshot().resources->at(id).bytes==r.bytes);assert(sha256(*r.bytes)==id);}
 std::stop_source stop;stop.request_stop();auto cancelled=originalLineartFillCommand(before,ink,fill,Id::fromNumber(25),1,1,red,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 if(save){auto path=std::filesystem::temp_directory_path()/("drawborad-original-poster-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");ProjectStore store;assert(store.save(path,doc.snapshot()));auto restored=store.recover(path);assert(restored&&restored.value().snapshot.sameContent(doc.snapshot()));std::filesystem::remove(path);}
 std::cout<<"PASS native-size original fill "<<w<<"x"<<h<<" area="<<area<<"\n";
}
int main(){
 poster(1725,2625,false);poster(3300,5100,true);
 NewDocumentRequest req;req.dimensions={16,16,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);
 auto made=Document::create(spec.value(),Id::fromNumber(1));assert(made);auto& doc=made.value();
 Bytes rgba(16*16*4,255);for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)if(x==0||x==8||x==15||y==0||y==15){auto i=(y*16+x)*4;rgba[i]=rgba[i+1]=rgba[i+2]=0;}
 const auto ink=Id::fromNumber(2),fill=Id::fromNumber(3);
 auto add=makeLineartRasterCommand(rgba,{'o','r','i','g'},16,16,ink);assert(add&&doc.apply(add.value(),0));
 auto layer=addFillLayerCommand(doc.snapshot(),fill,true,true);assert(layer&&doc.apply(layer.value(),doc.snapshot().revision));
 const auto original=doc.snapshot();ColorValue red;red.channels={1,0,0,0};red.alpha=1;
 auto command=originalLineartFillCommand(original,ink,fill,Id::fromNumber(4),3,3,0,red);assert(command);
 assert(doc.snapshot().sameContent(original));assert(doc.apply(command.value(),original.revision));const auto painted=doc.snapshot();
 for(const auto&[id,r]:*original.resources){assert(*painted.resources->at(id).bytes==*r.bytes);assert(sha256(*r.bytes)==id);}
 auto frame=renderMergedDocument(painted,{painted.revision});assert(frame);auto& pixels=frame.value().merged.bytes;
 assert(pixels[(3*16+3)*4]==255&&pixels[(3*16+3)*4+1]==0);assert(pixels[(3*16+12)*4+1]==255);
 ColorValue blue=red;blue.channels={0,0,1,0};auto again=originalLineartFillCommand(painted,ink,fill,Id::fromNumber(5),3,3,0,blue);assert(again);
 assert(std::get<Paint>(again.value().operations[0]).event.geometry.size()==std::get<Paint>(command.value().operations[0]).event.geometry.size());
 const auto& a=std::get<Paint>(command.value().operations[0]).event.geometry;const auto& b=std::get<Paint>(again.value().operations[0]).event.geometry;
 for(std::size_t i=0;i<a.size();++i)assert(a[i].x==b[i].x&&a[i].y==b[i].y);
 assert(doc.apply(again.value(),painted.revision));const auto twice=doc.snapshot();assert(doc.undo()&&doc.snapshot().sameContent(painted));assert(doc.redo()&&doc.snapshot().sameContent(twice));
 auto second=originalLineartFillCommand(doc.snapshot(),ink,fill,Id::fromNumber(6),12,3,0,red);assert(second&&doc.apply(second.value(),doc.snapshot().revision));
 auto both=renderMergedDocument(doc.snapshot(),{doc.snapshot().revision});assert(both);
 assert(both.value().merged.bytes[(3*16+3)*4+2]==255&&both.value().merged.bytes[(3*16+3)*4]==0);
 assert(both.value().merged.bytes[(3*16+12)*4]==255&&both.value().merged.bytes[(3*16+12)*4+2]==0);
 assert(!originalLineartFillCommand(doc.snapshot(),ink,fill,Id::fromNumber(7),8,3,0,red));
 assert(!originalLineartFillCommand(doc.snapshot(),ink,fill,Id::fromNumber(7),16,3,0,red));
 std::stop_source stop;stop.request_stop();assert(!originalLineartFillCommand(doc.snapshot(),ink,fill,Id::fromNumber(7),3,3,0,red,stop.get_token()));
 auto over=original;over.spec.pixelSize={8192,8192};auto denied=originalLineartFillCommand(over,ink,fill,Id::fromNumber(7),3,3,0,red);assert(!denied&&denied.error().code==ErrorCode::BudgetExceeded);
 auto nodes=std::make_shared<NodeMap>(*original.nodes);auto invalid=original;invalid.nodes=nodes;nodes->at(ink).locked=false;assert(!originalLineartFillCommand(invalid,ink,fill,Id::fromNumber(7),3,3,0,red));
 // New fixtures preserve valid resource hashes for colored/transparent checks.
 for(bool transparent:{false,true}){
  auto d=Document::create(spec.value(),Id::fromNumber(10));assert(d);auto raw=rgba;raw[(3*16+3)*4]=20;raw[(3*16+3)*4+1]=80;raw[(3*16+3)*4+2]=130;if(transparent)raw[(3*16+3)*4+3]=0;
  auto a=makeLineartRasterCommand(raw,{'s'},16,16,ink);assert(a&&d.value().apply(a.value(),0));auto f=addFillLayerCommand(d.value().snapshot(),fill,true,true);assert(f&&d.value().apply(f.value(),d.value().snapshot().revision));
  auto c=originalLineartFillCommand(d.value().snapshot(),ink,fill,Id::fromNumber(7),3,3,0,red);assert(bool(c)==transparent);
 }
 auto path=std::filesystem::temp_directory_path()/("drawborad-original-fill-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");
 ProjectStore store;assert(store.save(path,doc.snapshot()));auto reopened=store.recover(path);assert(reopened&&reopened.value().snapshot.sameContent(doc.snapshot()));std::filesystem::remove(path);
 std::cout<<"PASS original-only fill: independent regions, repeated fill, immutable resources, undo/redo/reopen, strict stack/seed/budget/cancel\n";
}
