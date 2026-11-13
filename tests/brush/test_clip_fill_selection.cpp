#include "brush/ClipFillSelection.h"
#include "brush/HollowInkRegion.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(){
 constexpr unsigned w=32,h=20;
 std::vector<std::uint8_t> ink(w*h,0);std::vector<Rgba8> image(w*h,{255,255,255,255});
 // Two wide blocks joined by a broad bridge are ONE connected component.
 for(unsigned y=3;y<17;y++)for(unsigned x=2;x<30;x++){
  if(x<12||x>=20||(y>=8&&y<12)){ink[y*w+x]=1;image[y*w+x]={0,0,0,255};}
 }
 const auto inkBefore=ink;auto all=floodFill(image,w,h,5,5,0);assert(all);const auto runsBefore=all.value().runs;const auto countBefore=all.value().pixelCount;
 auto clipped=clipFillSelection(all.value(),w,h,{0,0,16,h});assert(clipped);assert(clipped.value().pixelCount<all.value().pixelCount);
 auto hollow=hollowInkRegion(ink,clipped.value(),w,h,1);assert(hollow&&hollow.value().toClear.pixelCount>0);
 auto after=ink;for(auto r:hollow.value().toClear.runs)for(unsigned x=r.x;x<r.x+r.length;x++)after[r.y*w+x]=0;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  // Expected exact erosion of the selected left half. Right of cut, the
  // cut edge x=15 and original stroke boundaries must all remain untouched.
  bool expected=ink[y*w+x]&&x<16&&x>0&&y>0&&x+1<w&&y+1<h;
  if(expected)for(unsigned yy=y-1;yy<=y+1;yy++)for(unsigned xx=x-1;xx<=x+1;xx++)expected=expected&&xx<16&&ink[yy*w+xx];
  assert(after[y*w+x]==(expected?0:ink[y*w+x]));
  if(x>=15)assert(after[y*w+x]==ink[y*w+x]);
 }
 assert(ink==inkBefore&&all.value().runs==runsBefore&&all.value().pixelCount==countBefore);
 auto full=clipFillSelection(all.value(),w,h,{0,0,w,h});assert(full&&full.value().runs==runsBefore&&full.value().pixelCount==countBefore);
 auto single=clipFillSelection(all.value(),w,h,{2,3,1,1});assert(single&&single.value().pixelCount==1&&single.value().runs==std::vector<FillRun>({{3,2,1}}));
 auto empty=clipFillSelection(all.value(),w,h,{0,0,1,1});assert(!empty&&empty.error().messageKey=="clip-fill.empty-intersection");
 for(Rect invalid:std::vector<Rect>{{-1,0,1,1},{0,-1,1,1},{0,0,0,1},{0,0,1,0},{w,0,1,1},{0,h,1,1},{1,0,UINT64_MAX,1},{0,1,1,UINT64_MAX},{0,0,w+1,h}})assert(!clipFillSelection(all.value(),w,h,invalid));
 auto bad=all.value();bad.pixelCount++;assert(!clipFillSelection(bad,w,h,{0,0,w,h}));
 bad=all.value();bad.runs.back().length=w+1;assert(!clipFillSelection(bad,w,h,{0,0,1,1})); // invalid even outside rectangle
 bad=all.value();bad.runs[1]=bad.runs[0];assert(!clipFillSelection(bad,w,h,{0,0,w,h}));
 bad=all.value();std::swap(bad.runs.front(),bad.runs.back());assert(!clipFillSelection(bad,w,h,{0,0,w,h}));
 bad=all.value();bad.runs.front().length=0;assert(!clipFillSelection(bad,w,h,{0,0,w,h}));
 assert(!clipFillSelection(FillMask{},w,h,{0,0,w,h}));
 FillMask over;over.runs.resize(FillBudget{}.maxRuns+1);assert(!clipFillSelection(over,w,h,{0,0,w,h}));
 assert(!clipFillSelection(all.value(),8192,8192,{0,0,1,1}));assert(!clipFillSelection(all.value(),8193,1,{0,0,1,1}));
 std::stop_source stop;stop.request_stop();auto cancelled=clipFillSelection(all.value(),w,h,{0,0,w,h},stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 // Large original dimensions need no full raster buffer in this helper.
 FillMask large;large.runs={{5100-1,3300-2,2}};large.pixelCount=2;auto edge=clipFillSelection(large,3300,5100,{3299,5099,1,1});assert(edge&&edge.value().pixelCount==1);
 std::cout<<"PASS exact rectangle clip: connected bridge restricted, cut boundary/right untouched, input immutable, invalid/empty/cancel/budget guards\n";
}
