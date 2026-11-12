#include "DryInkCoverage.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace drawborad {
Result<std::vector<std::uint16_t>> dryInkCoverage(const StrokeEvent& e,Rect rect,std::uint64_t time,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"dry-ink.cancelled");
 auto valid=validateRasterEvent(e);if(!valid)return valid.error();
 if(e.engineVersion!=12 || e.toolId!="dry-ink")return error(ErrorCode::UnsupportedFeature,"dry-ink.engine");
 if(!rect.width||!rect.height||rect.width>TileSide||rect.height>TileSide || rect.x < -1000000000ll || rect.y < -1000000000ll || rect.x>1000000000ll || rect.y>1000000000ll)
  return error(ErrorCode::BudgetExceeded,"dry-ink.region");
 try{
  std::vector<std::uint16_t> coverage(std::size_t(rect.width*rect.height));
  if(time<e.startUs)return coverage;
  struct Nearest{double distance=std::numeric_limits<double>::infinity(),pressure=0,normal=0,along=0;};
  static_assert(sizeof(Nearest)==DryInkNearestBytesPerPixel);
  std::vector<Nearest> nearest(coverage.size());
  std::uint64_t work=0;
  auto unit=[&](std::int64_t fiber,std::int64_t node,std::uint64_t channel){
   std::uint64_t x=e.seed^(std::uint64_t(fiber)*0x9e3779b97f4a7c15ull)^(std::uint64_t(node)*0xbf58476d1ce4e5b9ull)^channel;
   x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;x^=x>>31;
   return double(x>>11)*(1./9007199254740992.);
  };
  auto segment=[&](double ax,double ay,double bx,double by,double pa,double pb,double arc,double tax,double tay,double tbx,double tby)->Result<void>{
   const double dx=bx-ax,dy=by-ay,length2=dx*dx+dy*dy;
   const double pad=e.style.width*std::max(pa,pb)*.5+.5;
   if(std::max(pa,pb)<=0)return {};
   const auto y0=std::max(rect.y,std::int64_t(std::floor(std::min(ay,by)-pad)));
   const auto y1=std::min(rect.y+std::int64_t(rect.height),std::int64_t(std::ceil(std::max(ay,by)+pad)));
   for(auto y=y0;y<y1;y++){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"dry-ink.cancelled");
    double lo=0,hi=1;
    if(dy!=0){const double t0=(double(y)+.5-ay-pad)/dy,t1=(double(y)+.5-ay+pad)/dy;lo=std::max(0.,std::min(t0,t1));hi=std::min(1.,std::max(t0,t1));if(lo>hi)continue;}
    const auto x0=std::max(rect.x,std::int64_t(std::floor(std::min(ax+dx*lo,ax+dx*hi)-pad)));
    const auto x1=std::min(rect.x+std::int64_t(rect.width),std::int64_t(std::ceil(std::max(ax+dx*lo,ax+dx*hi)+pad)));
    if(x1<=x0)continue;
    const auto cost=std::uint64_t(x1-x0);if(cost>32*1024*1024-work)return error(ErrorCode::BudgetExceeded,"dry-ink.work");work+=cost;
    for(auto x=x0;x<x1;x++){
     const double px=double(x)+.5,py=double(y)+.5;
     const double t=length2>0?std::clamp(((px-ax)*dx+(py-ay)*dy)/length2,0.,1.):0;
     const double pressure=length2>0?pa+(pb-pa)*t:std::max(pa,pb),width=e.style.width*pressure;
     const double distance=std::hypot(px-(ax+dx*t),py-(ay+dy*t));
     if(distance>width*.5+.5)continue;
     auto& selected=nearest[std::size_t(y-rect.y)*rect.width+std::size_t(x-rect.x)];
     if(distance>=selected.distance)continue; // deterministic first-segment tie
     const double len=std::sqrt(length2);
     const double tx=tax+(tbx-tax)*t,ty=tay+(tby-tay)*t;
     const double cross=tx*(py-(ay+dy*t))-ty*(px-(ax+dx*t));
     selected={distance,pressure,std::copysign(distance,cross),
       arc+(len>0?((px-ax)*dx+(py-ay)*dy)/len:0)};

    }
   }
   return {};
  };
  const auto& first=e.geometry.front();
  if(e.geometry.size()==1 && first.timestampUs<=time){auto done=segment(first.x,first.y,first.x,first.y,first.pressure,first.pressure,0,1,0,1,0);if(!done)return done.error();}
  auto tangent=[&](std::size_t index){
   const auto lo=index?index-1:index,hi=std::min(index+1,e.geometry.size()-1);
   double x=e.geometry[hi].x-e.geometry[lo].x,y=e.geometry[hi].y-e.geometry[lo].y;
   const double length=std::hypot(x,y);return std::pair<double,double>{length?x/length:1,length?y/length:0};
  };
  double arc=0;
  for(std::size_t i=1;i<e.geometry.size();i++){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"dry-ink.cancelled");
   const auto& a=e.geometry[i-1];const auto& b=e.geometry[i];if(a.timestampUs>time)break;
   long double fraction=1;
   if(b.timestampUs>time)fraction=(static_cast<long double>(time)-a.timestampUs)/(static_cast<long double>(b.timestampUs)-a.timestampUs);
   const double t=double(std::clamp(fraction,0.L,1.L));
   const auto ta=tangent(i-1),tb=tangent(i);
   auto done=segment(a.x,a.y,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.pressure,a.pressure+(b.pressure-a.pressure)*t,arc,ta.first,ta.second,ta.first+(tb.first-ta.first)*t,ta.second+(tb.second-ta.second)*t);if(!done)return done.error();
   arc+=std::hypot(b.x-a.x,b.y-a.y);
   if(b.timestampUs>time)break;
  }
  // Resolve texture once in the closest centerline frame. Different segment
  // textures must never max together: their interference produced mesh artifacts.
  for(std::size_t i=0;i<nearest.size();i++){
   if((i%rect.width)==0 && stop.stop_requested())return error(ErrorCode::Cancelled,"dry-ink.cancelled");
   const auto& n=nearest[i];if(!std::isfinite(n.distance))continue;
   const double width=e.style.width*n.pressure;
   const double edge=std::clamp(width*.5+.5-n.distance,0.,std::min(1.,width));
   // Seeded bristle positions/widths are fixed along the stroke. Ink supply
   // varies only through smooth, low-frequency arc-length nodes per bristle.
   // Integrate each bristle interval against a 1px normal-space box filter.
   const auto cell=std::int64_t(std::floor(n.normal/2.6));
   const double nodePosition=n.along/48.;const auto node=std::int64_t(std::floor(nodePosition));
   const double t=nodePosition-double(node),smooth=t*t*(3-2*t);
   if(5>32*1024*1024-work)return error(ErrorCode::BudgetExceeded,"dry-ink.work");
   work+=5;
   double ink=0;
   for(auto bristle=cell-2;bristle<=cell+2;++bristle){
    const double center=(double(bristle)+.5)*2.6+(unit(bristle,0,17)-.5)*2.;
    const double supply=unit(bristle,node,41)*(1-smooth)+unit(bristle,node+1,41)*smooth;
    const double contact=std::clamp((supply-(.56-.52*n.pressure))/.28,0.,1.);
    const double radius=(.25+unit(bristle,0,71)*1.15)*(.4+.9*n.pressure);
    const double overlap=std::max(0.,std::min(n.normal+.5,center+radius)-std::max(n.normal-.5,center-radius));
    ink+=overlap*contact;
   }
   ink=std::clamp(ink,0.,1.);
   const double textureMix=std::clamp(width/3.,0.,1.);
   coverage[i]=std::uint16_t(std::lround(edge*(.25+.75*n.pressure)*((1-textureMix)+textureMix*ink)*65535));
  }
  return coverage;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"dry-ink.allocation");}
}
}
