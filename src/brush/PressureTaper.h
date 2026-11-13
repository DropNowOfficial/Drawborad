#pragma once
#include "core/Document.h"
#include <algorithm>
#include <cmath>
#include <span>
namespace drawborad {
// Explicit mouse assistance, not measured pen pressure. Arc-length ramps do not
// move the centerline. Materialized pressure persists in ordinary native events.
inline Result<std::vector<InputSample>> taperMousePressure(std::span<const InputSample> input,double fraction) try {
 if(!std::isfinite(fraction)||fraction<0||fraction>.5||input.empty()||input.size()>1024)
  return error(ErrorCode::InvalidInput,"pressure-taper.input");
 std::vector<double> arc(input.size(),0);
 for(std::size_t i=0;i<input.size();++i){
  const auto& p=input[i];
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.pressure)||p.pressure<0||p.pressure>1)
   return error(ErrorCode::InvalidInput,"pressure-taper.input");
  if(i)arc[i]=arc[i-1]+std::hypot(p.x-input[i-1].x,p.y-input[i-1].y);
 }
 const double length=arc.back();
 if(!std::isfinite(length))return error(ErrorCode::InvalidInput,"pressure-taper.length");
 if(fraction==0||length<1e-9)return std::vector<InputSample>(input.begin(),input.end());
 const double ramp=length*fraction;if(ramp<=0)return error(ErrorCode::InvalidInput,"pressure-taper.length");const double knots[]={ramp,length-ramp};
 std::vector<InputSample> out;out.reserve(input.size()+2);
 const auto append=[&](InputSample sample,double distance){
  const double envelope=std::clamp(std::min(distance/ramp,(length-distance)/ramp),0.,1.);
  sample.pressure*=.05+.95*envelope;sample.timestampUs=out.size();out.push_back(sample);
 };
 append(input.front(),0);
 for(std::size_t i=1;i<input.size();++i){
  double last=-1;
  for(double knot:knots)if(knot>arc[i-1]&&knot<arc[i]&&knot!=last){
   const double t=(knot-arc[i-1])/(arc[i]-arc[i-1]);auto q=input[i-1];
   q.x+=(input[i].x-q.x)*t;q.y+=(input[i].y-q.y)*t;q.pressure+=(input[i].pressure-q.pressure)*t;
   append(q,knot);last=knot;
  }
  append(input[i],arc[i]);
 }
 // Remove only exactly collinear, linearly-pressure-redundant samples.
 // Otherwise identical mouse paths sampled at different browser rates can
 // acquire extra rounded segment caps in the legacy coverage renderer.
 std::vector<InputSample> compact;compact.reserve(out.size());
 for(const auto& point:out){
  while(compact.size()>=2){
   const auto& a=compact[compact.size()-2];const auto& b=compact.back();
   const double dx=point.x-a.x,dy=point.y-a.y,den=dx*dx+dy*dy;
   if(den<1e-18)break;
   const double t=((b.x-a.x)*dx+(b.y-a.y)*dy)/den;
   if(t<0||t>1||std::hypot(b.x-(a.x+t*dx),b.y-(a.y+t*dy))>1e-9||std::abs(b.pressure-(a.pressure+t*(point.pressure-a.pressure)))>1e-9)break;
   compact.pop_back();
  }
  compact.push_back(point);
 }
 for(std::size_t i=0;i<compact.size();++i)compact[i].timestampUs=i;
 return compact;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"pressure-taper.allocation");}
}
