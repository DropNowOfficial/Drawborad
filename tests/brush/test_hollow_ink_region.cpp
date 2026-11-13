#include "brush/HollowInkRegion.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
FillMask runs(const std::vector<std::uint8_t>& p,unsigned w,unsigned h){FillMask m;for(unsigned y=0;y<h;++y){for(unsigned x=0;x<w;){if(!p[y*w+x]){++x;continue;}auto start=x;while(x<w&&p[y*w+x])++x;m.runs.push_back({y,start,x-start});m.pixelCount+=x-start;}}return m;}
void verify(const std::vector<std::uint8_t>& ink,const std::vector<std::uint8_t>& selection,unsigned w,unsigned h,unsigned r){
 const auto original=ink;auto mask=runs(selection,w,h);auto result=hollowInkRegion(ink,mask,w,h,r);assert(result);std::vector<std::uint8_t> clear(ink.size(),0);for(auto run:result.value().toClear.runs)for(unsigned x=run.x;x<run.x+run.length;++x)clear[run.y*w+x]=1;
 std::uint64_t count=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){bool expected=x>=r&&y>=r&&x+r<w&&y+r<h;if(expected)for(unsigned yy=y-r;yy<=y+r;++yy)for(unsigned xx=x-r;xx<=x+r;++xx)expected&=bool(ink[yy*w+xx]&&selection[yy*w+xx]);assert(bool(clear[y*w+x])==expected);if(!selection[y*w+x])assert(!clear[y*w+x]);count+=clear[y*w+x];}
 assert(ink==original&&count==result.value().toClear.pixelCount&&result.value().preservedSelectedPixels+count==mask.pixelCount);
}
int main(){
 const unsigned w=16,h=16;std::vector<std::uint8_t> ink(w*h,1),select=ink;
 auto solid=hollowInkRegion(ink,runs(select,w,h),w,h,1);assert(solid&&solid.value().toClear.pixelCount==14*14);auto thick=hollowInkRegion(ink,runs(select,w,h),w,h,2);assert(thick&&thick.value().toClear.pixelCount==12*12);
 for(unsigned radius:{1u,2u}){
  verify(ink,select,w,h,radius);
  for(unsigned width=1;width<=2*radius;++width){std::vector<std::uint8_t> line(w*h,0);for(unsigned y=1;y<15;++y)for(unsigned x=4;x<4+width;++x)line[y*w+x]=1;auto a=hollowInkRegion(line,runs(line,w,h),w,h,radius);assert(a&&a.value().toClear.pixelCount==0);verify(line,line,w,h,radius);}
  std::vector<std::uint8_t> island(w*h,0);island[3*w+3]=1;auto singleton=hollowInkRegion(island,runs(island,w,h),w,h,radius);assert(singleton&&singleton.value().toClear.pixelCount==0&&singleton.value().preservedSelectedPixels==1);
  auto hole=ink;for(unsigned y=5;y<10;++y)for(unsigned x=5;x<10;++x)hole[y*w+x]=0;verify(hole,hole,w,h,radius);
  auto concave=ink;for(unsigned y=0;y<9;++y)for(unsigned x=8;x<w;++x)concave[y*w+x]=0;verify(concave,concave,w,h,radius);
  std::vector<std::uint8_t> diagonal(w*h,0);for(unsigned y=1;y<7;++y)for(unsigned x=1;x<7;++x)diagonal[y*w+x]=1;for(unsigned y=7;y<14;++y)for(unsigned x=7;x<14;++x)diagonal[y*w+x]=1;verify(diagonal,diagonal,w,h,radius);
  auto cropped=select;for(unsigned y=0;y<h;++y)for(unsigned x=8;x<w;++x)cropped[y*w+x]=0;verify(ink,cropped,w,h,radius);auto a=hollowInkRegion(ink,runs(cropped,w,h),w,h,radius);assert(a);for(auto run:a.value().toClear.runs)assert(run.x+run.length<=8-radius);
 }
 auto mask=runs(ink,w,h);std::stop_source stop;stop.request_stop();auto cancelled=hollowInkRegion(ink,mask,w,h,1,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 assert(!hollowInkRegion(ink,mask,w,h,0));assert(!hollowInkRegion(ink,mask,w,h,3));assert(!hollowInkRegion(ink,mask,8192,8192,1));auto bad=ink;bad[0]=2;assert(!hollowInkRegion(bad,mask,w,h,1));bad[0]=0;assert(!hollowInkRegion(bad,mask,w,h,1));auto wrong=mask;wrong.pixelCount++;assert(!hollowInkRegion(ink,wrong,w,h,1));wrong=mask;wrong.runs.push_back(mask.runs.front());assert(!hollowInkRegion(ink,wrong,w,h,1));
 std::cout<<"PASS local hollow: exact square-neighbourhood erosion, selection-cut border, thin marks, holes/concavity/diagonal, no source mutation, budgets/cancel\n";
}
