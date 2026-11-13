#include "FineLineCoverage.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
Result<std::vector<std::uint16_t>> fineLineCoverage(const StrokeEvent& e,Rect rect,std::uint64_t time,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"fine-line.cancelled");
 auto valid=validateRasterEvent(e);if(!valid)return valid.error();
 if(!((e.engineVersion==10 && e.toolId=="fineliner-aa") || (e.engineVersion==19 && e.toolId=="smooth-ink-aa")))return error(ErrorCode::UnsupportedFeature,"fine-line.engine");
 if(!rect.width||!rect.height||rect.width>TileSide||rect.height>TileSide || rect.x < -1000000000ll || rect.y < -1000000000ll || rect.x>1000000000ll || rect.y>1000000000ll)
  return error(ErrorCode::BudgetExceeded,"fine-line.region");
 try{
  std::vector<std::uint16_t> coverage(std::size_t(rect.width*rect.height));
  if(time<e.startUs)return coverage;
  std::uint64_t work=0;
  auto segment=[&](double ax,double ay,double bx,double by,double pa,double pb)->Result<void>{
   const double dx=bx-ax,dy=by-ay,length2=dx*dx+dy*dy;
   const double pad=e.style.width*std::max(pa,pb)*.5+.5;
   if(std::max(pa,pb)<=0)return {};
   const auto y0=std::max(rect.y,std::int64_t(std::floor(std::min(ay,by)-pad)));
   const auto y1=std::min(rect.y+std::int64_t(rect.height),std::int64_t(std::ceil(std::max(ay,by)+pad)));
   for(auto y=y0;y<y1;y++){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"fine-line.cancelled");
    double lo=0,hi=1;
    if(dy!=0){const double t0=(double(y)+.5-ay-pad)/dy,t1=(double(y)+.5-ay+pad)/dy;lo=std::max(0.,std::min(t0,t1));hi=std::min(1.,std::max(t0,t1));if(lo>hi)continue;}
    const auto x0=std::max(rect.x,std::int64_t(std::floor(std::min(ax+dx*lo,ax+dx*hi)-pad)));
    const auto x1=std::min(rect.x+std::int64_t(rect.width),std::int64_t(std::ceil(std::max(ax+dx*lo,ax+dx*hi)+pad)));
    if(x1<=x0)continue;
    const auto cost=std::uint64_t(x1-x0);if(cost>32*1024*1024-work)return error(ErrorCode::BudgetExceeded,"fine-line.work");work+=cost;
    for(auto x=x0;x<x1;x++){
     const double px=double(x)+.5,py=double(y)+.5;
     const double t=length2>0?std::clamp(((px-ax)*dx+(py-ay)*dy)/length2,0.,1.):0;
     const double pressure=length2>0?pa+(pb-pa)*t:std::max(pa,pb),width=e.style.width*pressure;
     const double distance=std::hypot(px-(ax+dx*t),py-(ay+dy*t));
     // Symmetric one-pixel reconstruction filter, capped by the physical thin
     // stroke width. This is antialiased coverage, not opacity compensation.
     const double alpha=std::clamp(width*.5+.5-distance,0.,std::min(1.,width));
     const auto value=std::uint16_t(std::lround(alpha*65535));
     auto& target=coverage[std::size_t(y-rect.y)*rect.width+std::size_t(x-rect.x)];target=std::max(target,value);
    }
   }
   return {};
  };
  const auto& first=e.geometry.front();
  if(first.timestampUs<=time){auto done=segment(first.x,first.y,first.x,first.y,first.pressure,first.pressure);if(!done)return done.error();}
  for(std::size_t i=1;i<e.geometry.size();i++){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"fine-line.cancelled");
   const auto& a=e.geometry[i-1];const auto& b=e.geometry[i];if(a.timestampUs>time)break;
   long double fraction=1;
   if(b.timestampUs>time)fraction=(static_cast<long double>(time)-a.timestampUs)/(static_cast<long double>(b.timestampUs)-a.timestampUs);
   const double t=double(std::clamp(fraction,0.L,1.L));
   auto done=segment(a.x,a.y,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.pressure,a.pressure+(b.pressure-a.pressure)*t);if(!done)return done.error();
   if(b.timestampUs>time)break;
  }
  return coverage;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"fine-line.allocation");}
}
}
