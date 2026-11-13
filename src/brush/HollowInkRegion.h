#pragma once
#include "FloodFill.h"
#include <algorithm>
#include <span>
namespace drawborad {
struct HollowInkRegionResult {
 FillMask toClear;
 std::uint64_t selectedPixels{},preservedSelectedPixels{};
};
// Local binary-mask hollowing, NOT skeletonization. The entire square radius
// neighbourhood must be selected ink. Thus both ink edges/holes and selection
// cut edges are retained. Outside canvas is non-ink. No smoothing or resizing.
// Scratch <=2*w*h bytes (40MB at 20M), plus <=65536 output runs.
inline Result<HollowInkRegionResult> hollowInkRegion(std::span<const std::uint8_t> ink,
 const FillMask& selected,std::uint32_t w,std::uint32_t h,unsigned radius,
 std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
 const auto n=std::uint64_t(w)*h;
 if(!w||!h||w>8192||h>8192||n>20000000||selected.runs.size()>FillBudget{}.maxRuns)
  return error(ErrorCode::BudgetExceeded,"hollow.budget");
 if(ink.size()!=n||(radius!=1&&radius!=2)||selected.runs.empty()||!selected.pixelCount)
  return error(ErrorCode::InvalidInput,"hollow.input");
 for(std::size_t i=0;i<ink.size();++i){if(i%65536==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");if(ink[i]>1)return error(ErrorCode::InvalidInput,"hollow.binary-required");}
 FillRun previous{};bool first=true;std::uint64_t count=0;
 for(const auto& r:selected.runs){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
  if(!r.length||r.y>=h||r.x>=w||r.length>w-r.x||(!first&&(r.y<previous.y||(r.y==previous.y&&r.x<previous.x+previous.length))))return error(ErrorCode::InvalidInput,"hollow.selection-runs");
  for(unsigned x=r.x;x<r.x+r.length;++x)if(!ink[std::size_t(r.y)*w+x])return error(ErrorCode::InvalidInput,"hollow.selection-not-ink");
  count+=r.length;previous=r;first=false;
 }
 if(count!=selected.pixelCount)return error(ErrorCode::InvalidInput,"hollow.selection-count");
 std::vector<std::uint8_t> chosen(std::size_t(n),0),horizontal(std::size_t(n),0);
 for(auto r:selected.runs){if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");std::fill_n(chosen.begin()+std::size_t(r.y)*w+r.x,r.length,1);}
 for(unsigned y=0;y<h;++y){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
  for(unsigned x=radius;x+radius<w;++x){bool full=true;for(unsigned xx=x-radius;xx<=x+radius;++xx)full&=bool(chosen[std::size_t(y)*w+xx]);horizontal[std::size_t(y)*w+x]=full;}
 }
 HollowInkRegionResult out;out.selectedPixels=count;
 for(unsigned y=radius;y+radius<h;++y){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
  unsigned start=0,length=0;
  for(unsigned x=0;x<=w;++x){bool clear=x<w;
   if(clear)for(unsigned yy=y-radius;yy<=y+radius;++yy)clear&=bool(horizontal[std::size_t(yy)*w+x]);
   if(clear){if(!length)start=x;++length;}
   else if(length){if(out.toClear.runs.size()>=FillBudget{}.maxRuns)return error(ErrorCode::BudgetExceeded,"hollow.output-runs");out.toClear.runs.push_back({y,start,length});out.toClear.pixelCount+=length;length=0;}
  }
 }
 out.preservedSelectedPixels=count-out.toClear.pixelCount;return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"hollow.allocation");}
}
