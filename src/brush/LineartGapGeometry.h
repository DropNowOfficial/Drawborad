#pragma once
#include "core/BucketEvent.h"
#include "view/Viewport.h"
#include <algorithm>
namespace drawborad {
// Canonical integer 1px Bresenham, at most 64 pixels including anchors.
// Endpoints are original pixel indices, not subpixel coordinates.
inline Result<std::vector<std::uint32_t>> lineartGapPixels(DocumentPoint a,DocumentPoint b,std::uint32_t w,std::uint32_t h){
 if(!w||!h||w>8192||h>8192||std::uint64_t(w)*h>20000000)return error(ErrorCode::BudgetExceeded,"gap.canvas-limit");
 for(const auto p:{a,b})if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>=w||p.y>=h||std::floor(p.x)!=p.x||std::floor(p.y)!=p.y)return error(ErrorCode::InvalidInput,"gap.endpoints");
 if(a.y>b.y||(a.y==b.y&&a.x>b.x))std::swap(a,b);
 int x=int(a.x),y=int(a.y),lastX=int(b.x),lastY=int(b.y);const int dx=std::abs(lastX-x),dy=-std::abs(lastY-y),sx=x<lastX?1:-1,sy=y<lastY?1:-1;
 if(std::max(dx,-dy)+1>64||(!dx&&!dy))return error(ErrorCode::BudgetExceeded,"gap.length");
 int err=dx+dy;std::vector<std::uint32_t> out;out.reserve(64);
 for(;;){out.push_back(std::uint32_t(y)*w+std::uint32_t(x));if(x==lastX&&y==lastY)break;const int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
 std::sort(out.begin(),out.end());return out;
}
inline Result<std::vector<std::uint32_t>> validateLineartGapEvent(const StrokeEvent& e,std::span<const std::uint8_t> raw,
 std::uint32_t w,std::uint32_t h){
 if(!w||!h||w>8192||h>8192||std::uint64_t(w)*h>20000000)return error(ErrorCode::BudgetExceeded,"gap.canvas-limit");
 auto checked=validateBucketEvent(e);if(!checked)return checked.error();
 if(raw.size()!=std::size_t(w)*h*4)return error(ErrorCode::InvalidInput,"gap.source-size");
 std::vector<std::uint32_t> pixels;
 for(std::size_t i=0;i<e.geometry.size();i+=2){const auto& a=e.geometry[i];const auto& b=e.geometry[i+1];
  if(a.y>=h||b.x>w||b.x-a.x>64||pixels.size()+std::size_t(b.x-a.x)>64)return error(ErrorCode::BudgetExceeded,"gap.length");
  for(auto x=std::uint32_t(a.x);x<std::uint32_t(b.x);x++)pixels.push_back(std::uint32_t(a.y)*w+x);
 }
 if(pixels.size()<3)return error(ErrorCode::InvalidInput,"gap.no-white-crossing");
 auto shape=lineartGapPixels({double(pixels.front()%w),double(pixels.front()/w)},{double(pixels.back()%w),double(pixels.back()/w)},w,h);
 if(!shape||shape.value()!=pixels)return error(ErrorCode::InvalidInput,"gap.not-straight-bresenham");
 const auto isInk=[&](std::uint32_t p){const auto k=std::size_t(p)*4;return raw[k+3]&&raw[k]<255&&raw[k]==raw[k+1]&&raw[k+1]==raw[k+2];};
 if(!isInk(pixels.front())||!isInk(pixels.back()))return error(ErrorCode::InvalidInput,"gap.anchors-not-original-ink");
 bool white=false;for(auto p:pixels){const auto k=std::size_t(p)*4;if(raw[k+3]&&raw[k]==255&&raw[k+1]==255&&raw[k+2]==255)white=true;else if(!isInk(p))return error(ErrorCode::UnsupportedFeature,"gap.transparent-or-colored-crossing");}
 if(!white)return error(ErrorCode::InvalidInput,"gap.no-white-crossing");
 return pixels;
}
}
