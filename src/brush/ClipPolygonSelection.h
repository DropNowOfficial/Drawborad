#pragma once
#include "FloodFill.h"
#include "view/Viewport.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
namespace clip_polygon_detail {
inline long double cross(DocumentPoint a,DocumentPoint b,DocumentPoint c){return (static_cast<long double>(b.x)-a.x)*(static_cast<long double>(c.y)-a.y)-(static_cast<long double>(b.y)-a.y)*(static_cast<long double>(c.x)-a.x);}
// Conservative near-collinearity tolerance, in squared original pixels.
constexpr long double epsilon=1e-10L;
inline int sign(long double x){return x>epsilon?1:x<-epsilon?-1:0;}
inline bool onSegment(DocumentPoint a,DocumentPoint b,DocumentPoint p){return sign(cross(a,b,p))==0&&p.x>=std::min(a.x,b.x)-epsilon&&p.x<=std::max(a.x,b.x)+epsilon&&p.y>=std::min(a.y,b.y)-epsilon&&p.y<=std::max(a.y,b.y)+epsilon;}
inline bool intersect(DocumentPoint a,DocumentPoint b,DocumentPoint c,DocumentPoint d){
 const auto abC=sign(cross(a,b,c)),abD=sign(cross(a,b,d)),cdA=sign(cross(c,d,a)),cdB=sign(cross(c,d,b));
 return (abC*abD<0&&cdA*cdB<0)||(!abC&&onSegment(a,b,c))||(!abD&&onSegment(a,b,d))||(!cdA&&onSegment(c,d,a))||(!cdB&&onSegment(c,d,b));
}
}
// Implicit closing edge: caller must NOT repeat first vertex at the end.
// Reject ambiguous near-degenerate geometry rather than silently repair it.
inline Result<void> validateClipPolygon(std::span<const DocumentPoint> polygon,
 std::uint32_t w,std::uint32_t h,std::stop_token stop={}){
 using namespace clip_polygon_detail;
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-polygon.cancelled");
 if(!w||!h||w>8192||h>8192||std::uint64_t(w)*h>20000000||polygon.size()>512)return error(ErrorCode::BudgetExceeded,"clip-polygon.budget");
 if(polygon.size()<3)return error(ErrorCode::InvalidInput,"clip-polygon.vertices");
 for(const auto p:polygon)if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>w||p.y>h)return error(ErrorCode::InvalidInput,"clip-polygon.bounds");
 long double area=0;const auto n=polygon.size();
 for(std::size_t i=0;i<n;++i){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-polygon.cancelled");
  const auto a=polygon[i],b=polygon[(i+1)%n],c=polygon[(i+2)%n];
  const auto dx=static_cast<long double>(a.x)-b.x,dy=static_cast<long double>(a.y)-b.y;
  if(dx*dx+dy*dy<=epsilon*epsilon)return error(ErrorCode::InvalidInput,"clip-polygon.zero-edge");
  if(sign(cross(a,b,c))==0&&((static_cast<long double>(b.x)-a.x)*(static_cast<long double>(c.x)-b.x)+(static_cast<long double>(b.y)-a.y)*(static_cast<long double>(c.y)-b.y))<0)return error(ErrorCode::InvalidInput,"clip-polygon.backtrack");
  area+=static_cast<long double>(a.x)*b.y-static_cast<long double>(a.y)*b.x;
  for(std::size_t j=i+1;j<n;++j){
   if(j==i+1||(i==0&&j==n-1))continue;
   if(intersect(a,b,polygon[j],polygon[(j+1)%n]))return error(ErrorCode::InvalidInput,"clip-polygon.self-intersection");
  }
 }
 if(std::abs(area)<=epsilon)return error(ErrorCode::InvalidInput,"clip-polygon.zero-area");
 return {};
}
// Pixel centers, even-odd with half-open crossing intervals [left,right).
// O(V²) validation; each occupied row O(V log V + runs + intervals), V<=512.
// No full-image storage: <=512 intersections plus <=65536 output runs.
inline Result<FillMask> clipPolygonSelection(const FillMask& mask,
 std::uint32_t w,std::uint32_t h,std::span<const DocumentPoint> polygon,std::stop_token stop={}) try {
 auto valid=validateClipPolygon(polygon,w,h,stop);if(!valid)return valid.error();
 if(mask.runs.size()>FillBudget{}.maxRuns)return error(ErrorCode::BudgetExceeded,"clip-polygon.runs-budget");
 if(mask.runs.empty()||!mask.pixelCount)return error(ErrorCode::InvalidInput,"clip-polygon.empty-mask");
 std::uint64_t count=0;FillRun prev{};bool first=true;
 for(const auto& r:mask.runs){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-polygon.cancelled");
  if(!r.length||r.y>=h||r.x>=w||r.length>w-r.x||(!first&&(r.y<prev.y||(r.y==prev.y&&r.x<prev.x+prev.length))))return error(ErrorCode::InvalidInput,"clip-polygon.runs");
  count+=r.length;prev=r;first=false;
 }
 if(count!=mask.pixelCount)return error(ErrorCode::InvalidInput,"clip-polygon.count");
 FillMask out;std::vector<long double> crossings;crossings.reserve(polygon.size());
 for(std::size_t begin=0;begin<mask.runs.size();){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-polygon.cancelled");
  auto end=begin+1;const auto row=mask.runs[begin].y;while(end<mask.runs.size()&&mask.runs[end].y==row)++end;
  const long double cy=static_cast<long double>(row)+.5L;crossings.clear();
  for(std::size_t i=0;i<polygon.size();++i){
   const auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
   if((a.y<=cy&&cy<b.y)||(b.y<=cy&&cy<a.y))crossings.push_back(a.x+(cy-a.y)*(static_cast<long double>(b.x)-a.x)/(static_cast<long double>(b.y)-a.y));
  }
  std::sort(crossings.begin(),crossings.end());
  if(crossings.size()%2)return error(ErrorCode::InvalidInput,"clip-polygon.ambiguous-crossings");
  auto runIndex=begin;
  for(std::size_t k=0;k<crossings.size();k+=2){
   const auto left=std::uint32_t(std::clamp(std::ceil(crossings[k]-.5L),0.L,static_cast<long double>(w)));
   const auto right=std::uint32_t(std::clamp(std::ceil(crossings[k+1]-.5L),0.L,static_cast<long double>(w)));
   while(runIndex<end&&mask.runs[runIndex].x+mask.runs[runIndex].length<=left)++runIndex;
   for(auto j=runIndex;j<end&&mask.runs[j].x<right;++j){
    const auto& r=mask.runs[j];const auto a=std::max(left,r.x),b=std::min(right,r.x+r.length);
    if(a>=b)continue;
    if(out.runs.size()>=FillBudget{}.maxRuns)return error(ErrorCode::BudgetExceeded,"clip-polygon.output-runs");
    out.runs.push_back({row,a,b-a});out.pixelCount+=b-a;
   }
  }
  begin=end;
 }
 if(!out.pixelCount)return error(ErrorCode::InvalidInput,"clip-polygon.empty-intersection");
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"clip-polygon.allocation");}
}
