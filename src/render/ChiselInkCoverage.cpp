#include "ChiselInkCoverage.h"
#include "core/ChiselInkEvent.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace drawborad {
Result<std::vector<std::uint16_t>> chiselInkCoverage(const StrokeEvent&e,Rect rect,std::uint64_t time,std::stop_token stop) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"chisel-ink.cancelled");
 auto valid=validateChiselInkEvent(e);if(!valid)return valid.error();
 if(!rect.width||!rect.height||rect.width>TileSide||rect.height>TileSide||rect.x < -1000000000ll||rect.y < -1000000000ll||rect.x>1000000000ll||rect.y>1000000000ll)return error(ErrorCode::BudgetExceeded,"chisel-ink.region");
 std::vector<std::uint16_t> masks(std::size_t(rect.width*rect.height),0);if(time<e.startUs)return masks;
 constexpr long double cos22=.923879532511286756128183189396788287L,sin22=.382683432365089771728459984030398866L;
 constexpr long double diagonal=.707106781186547524400844362104849039L;
 constexpr std::array<std::array<long double,2>,8> directions{{{1,0},{cos22,sin22},{diagonal,diagonal},{sin22,cos22},{0,1},{-sin22,cos22},{-diagonal,diagonal},{-cos22,sin22}}};
 const auto co=directions[e.style.angle][0],si=directions[e.style.angle][1];std::uint64_t work=0;
 auto segment=[&](double ax,double ay,double bx,double by,double pa,double pb)->Result<void>{
 if(std::max(pa,pb)<=0)return {};
 const long double dx=static_cast<long double>(bx)-ax,dy=static_cast<long double>(by)-ay,vx=co*dx+si*dy,vy=3*(-si*dx+co*dy),r0=e.style.width*pa*.5L,dr=e.style.width*(static_cast<long double>(pb)-pa)*.5L,A=vx*vx+vy*vy-dr*dr;
 const double pad=e.style.width*std::max(pa,pb)*.5;
 const auto y0=std::max(rect.y,std::int64_t(std::floor(std::min(ay,by)-pad))),y1=std::min(rect.y+std::int64_t(rect.height),std::int64_t(std::ceil(std::max(ay,by)+pad)));
 for(auto y=y0;y<y1;++y){if(stop.stop_requested())return error(ErrorCode::Cancelled,"chisel-ink.cancelled");
 long double lo=0,hi=1;if(dy!=0){const auto t0=(y+.5L-ay-pad-.5L)/dy,t1=(y+.5L-ay+pad+.5L)/dy;lo=std::max(0.L,std::min(t0,t1));hi=std::min(1.L,std::max(t0,t1));if(lo>hi)continue;}
 const auto x0=std::max(rect.x,std::int64_t(std::floor(std::min(ax+dx*lo,ax+dx*hi)-pad))),x1=std::min(rect.x+std::int64_t(rect.width),std::int64_t(std::ceil(std::max(ax+dx*lo,ax+dx*hi)+pad)));
 if(x1<=x0)continue;
 const auto cost=std::uint64_t(x1-x0)*16;if(cost>32ull*1024*1024-work)return error(ErrorCode::BudgetExceeded,"chisel-ink.work");work+=cost;
 for(auto x=x0;x<x1;++x){auto&mask=masks[std::size_t(y-rect.y)*rect.width+std::size_t(x-rect.x)];for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx){const unsigned bit=1u<<(4*sy+sx);if(mask&bit)continue;
 const long double px=x+(sx+.5L)/4-ax,py=y+(sy+.5L)/4-ay,qx=co*px+si*py,qy=3*(-si*px+co*py),B=-2*(qx*vx+qy*vy+r0*dr);
 auto member=[&](long double t){const auto u=qx-t*vx,v=qy-t*vy,r=r0+t*dr;return r>0&&u*u+v*v<=r*r;};bool covered=member(0)||member(1);if(!covered&&A>0)covered=member(std::clamp(-B/(2*A),0.L,1.L));if(covered)mask|=std::uint16_t(bit);
 }}
 }return {};
 };
 const auto&first=e.geometry.front();auto done=segment(first.x,first.y,first.x,first.y,first.pressure,first.pressure);if(!done)return done.error();
 for(std::size_t i=1;i<e.geometry.size();++i){if(stop.stop_requested())return error(ErrorCode::Cancelled,"chisel-ink.cancelled");const auto&a=e.geometry[i-1];const auto&b=e.geometry[i];if(a.timestampUs>time)break;long double fraction=1;if(b.timestampUs>time)fraction=(static_cast<long double>(time)-a.timestampUs)/(static_cast<long double>(b.timestampUs)-a.timestampUs);const double t=double(std::clamp(fraction,0.L,1.L));done=segment(a.x,a.y,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.pressure,a.pressure+(b.pressure-a.pressure)*t);if(!done)return done.error();if(b.timestampUs>time)break;}
 for(auto&mask:masks)mask=std::uint16_t((std::popcount(mask)*65535u+8)/16);
 return masks;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"chisel-ink.allocation");}
}
