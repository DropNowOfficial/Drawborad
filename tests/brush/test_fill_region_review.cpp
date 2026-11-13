#include "brush/FillRegionReview.h"
#include "core/Document.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
FillMask rectangle(unsigned x,unsigned y,unsigned w,unsigned h){FillMask m;for(unsigned row=y;row<y+h;++row)m.runs.push_back({row,x,w});m.pixelCount=std::uint64_t(w)*h;return m;}
int main(){
 auto m=rectangle(2,2,5,5);const auto original=m.runs;auto r=reviewFillRegion(m,10,10);assert(r);assert(r.value().pixelCount==25&&r.value().bounds==Rect({2,2,5,5}));assert(!r.value().touchesCanvasEdge&&!r.value().tiny);assert(r.value().seedX==4&&r.value().seedY==4&&r.value().clearance==3);assert(m.runs.size()==original.size());for(std::size_t i=0;i<m.runs.size();++i)assert(m.runs[i].x==original[i].x&&m.runs[i].y==original[i].y&&m.runs[i].length==original[i].length);
 auto tiny=reviewFillRegion(rectangle(3,4,2,1),10,10);assert(tiny&&tiny.value().tiny&&tiny.value().pixelCount==2&&tiny.value().seedX==3&&tiny.value().seedY==4&&tiny.value().clearance==1);
 auto edge=reviewFillRegion(rectangle(0,0,10,10),10,10);assert(edge&&edge.value().touchesCanvasEdge&&edge.value().seedX==4&&edge.value().seedY==4&&edge.value().clearance==5);
 // A one-pixel ring: preserve its central hole, not replace it with a rectangle.
 FillMask ring;ring.runs={{1,1,5},{2,1,1},{2,5,1},{3,1,1},{3,5,1},{4,1,1},{4,5,1},{5,1,5}};ring.pixelCount=16;
 auto hole=reviewFillRegion(ring,8,8);assert(hole&&hole.value().clearance==1&&hole.value().seedX==1&&hole.value().seedY==1&&hole.value().pixelCount==16);
 auto thin=reviewFillRegion(rectangle(2,2,1,6),10,10);assert(thin&&!thin.value().tiny&&thin.value().clearance==1);
 // Bucket Paint uses half-open run endpoints. Reconstruct exactly, including gaps.
 StrokeEvent event;for(auto run:ring.runs){InputSample a,b;a.x=run.x;a.y=run.y;b.x=run.x+run.length;b.y=run.y;event.geometry.push_back(a);event.geometry.push_back(b);}
 FillMask restored;for(std::size_t i=0;i<event.geometry.size();i+=2){auto a=event.geometry[i],b=event.geometry[i+1];auto length=std::uint32_t(b.x-a.x);restored.runs.push_back({std::uint32_t(a.y),std::uint32_t(a.x),length});restored.pixelCount+=length;}
 auto roundtrip=reviewFillRegion(restored,8,8);assert(roundtrip&&roundtrip.value().bounds==hole.value().bounds&&roundtrip.value().seedX==hole.value().seedX&&roundtrip.value().clearance==hole.value().clearance);
 auto invalid=m;invalid.pixelCount++;assert(!reviewFillRegion(invalid,10,10));invalid=m;invalid.runs.push_back(invalid.runs.front());assert(!reviewFillRegion(invalid,10,10));invalid=m;invalid.runs[0].length=UINT32_MAX;assert(!reviewFillRegion(invalid,10,10));assert(!reviewFillRegion({},10,10));assert(!reviewFillRegion(m,8193,10));assert(!reviewFillRegion(m,8192,8192));
 std::stop_source stop;stop.request_stop();auto cancelled=reviewFillRegion(m,10,10,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 for(auto size:{PixelSize{1725,2625},PixelSize{3300,5100}}){auto big=rectangle(1,1,size.width-2,size.height-2);auto b=reviewFillRegion(big,size.width,size.height);assert(b&&b.value().pixelCount==(size.width-2)*(size.height-2)&&!b.value().touchesCanvasEdge&&b.value().clearance==(size.width-1)/2);}
 std::cout<<"PASS region review: unchanged runs, seed clearance, holes, narrow/tiny, edge, bucket-pair roundtrip, budgets/cancel, native poster sizes\n";
}
