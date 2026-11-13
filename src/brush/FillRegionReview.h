#pragma once
#include "FloodFill.h"
#include "core/Types.h"
#include <algorithm>
#include <limits>
namespace drawborad {
struct FillRegionReview {
 std::uint64_t pixelCount{};
 Rect bounds{};
 bool touchesCanvasEdge{},tiny{};
 std::uint32_t seedX{},seedY{};
 // Manhattan distance to nearest non-region pixel centre; outside canvas
 // counts as non-region. A single pixel has clearance 1, not a 1px margin.
 std::uint16_t clearance{};
};
// Read-only review of sorted nonoverlapping scanline runs (the same representation
// as paired bucket Paint points). No topology changes or small-island removal.
// Scratch: 2*w*h bytes, at most 40,000,000 bytes; excludes caller-owned mask.
inline Result<FillRegionReview> reviewFillRegion(const FillMask& mask,
 std::uint32_t w,std::uint32_t h,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
 const auto n=std::uint64_t(w)*h;
 if(!w||!h||w>8192||h>8192||n>20000000||mask.runs.size()>FillBudget{}.maxRuns)
  return error(ErrorCode::BudgetExceeded,"fill-review.budget");
 if(mask.runs.empty()||!mask.pixelCount)return error(ErrorCode::InvalidInput,"fill-review.empty");
 std::uint64_t count=0;std::uint32_t minX=w,minY=h,maxX=0,maxY=0;
 FillRun previous{};bool first=true,edge=false;
 for(const auto& r:mask.runs){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
  if(!r.length||r.y>=h||r.x>=w||r.length>w-r.x||
     (!first&&(r.y<previous.y||(r.y==previous.y&&r.x<previous.x+previous.length))))
   return error(ErrorCode::InvalidInput,"fill-review.runs");
  count+=r.length;minX=std::min(minX,r.x);minY=std::min(minY,r.y);
  maxX=std::max(maxX,r.x+r.length);maxY=std::max(maxY,r.y+1);
  edge|=r.y==0||r.y==h-1||r.x==0||r.x+r.length==w;
  previous=r;first=false;
 }
 if(count!=mask.pixelCount)return error(ErrorCode::InvalidInput,"fill-review.count");
 const auto infinity=std::uint16_t(w+h+1);
 std::vector<std::uint16_t> distance(std::size_t(n),0);
 for(const auto& r:mask.runs){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
  std::fill_n(distance.begin()+std::size_t(r.y)*w+r.x,r.length,infinity);
 }
 for(std::uint32_t y=0;y<h;++y){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
  for(std::uint32_t x=0;x<w;++x){auto& d=distance[std::size_t(y)*w+x];if(!d)continue;
   const auto left=x?distance[std::size_t(y)*w+x-1]:0;
   const auto up=y?distance[std::size_t(y-1)*w+x]:0;
   d=std::min(d,std::uint16_t(1+std::min(left,up)));
  }
 }
 for(std::uint32_t y=h;y-->0;){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
  for(std::uint32_t x=w;x-->0;){auto& d=distance[std::size_t(y)*w+x];if(!d)continue;
   const auto right=x+1<w?distance[std::size_t(y)*w+x+1]:0;
   const auto down=y+1<h?distance[std::size_t(y+1)*w+x]:0;
   d=std::min(d,std::uint16_t(1+std::min(right,down)));
  }
 }
 FillRegionReview out;out.pixelCount=count;out.bounds={minX,minY,maxX-minX,maxY-minY};out.touchesCanvasEdge=edge;out.tiny=count<=4;
 for(std::uint32_t y=0;y<h;++y){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"fill-review.cancelled");
  for(std::uint32_t x=0;x<w;++x){const auto d=distance[std::size_t(y)*w+x];if(d>out.clearance){out.clearance=d;out.seedX=x;out.seedY=y;}}
 }
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"fill-review.allocation");}
}
