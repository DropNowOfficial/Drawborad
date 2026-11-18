#include "DryInkCoverage.h"
#include "core/ArtistOptIn.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace drawborad {
namespace {
double hash2d(std::int64_t ix, std::int64_t iy, std::uint64_t seed, std::uint64_t octave) noexcept {
 std::uint64_t h = seed ^ (std::uint64_t(ix) * 0x9e3779b97f4a7c15ull) ^
                   (std::uint64_t(iy) * 0xbf58476d1ce4e5b9ull) ^
                   (octave * 0x94d049bb133111ebull);
 h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
 h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
 h ^= h >> 31;
 return double(h >> 11) * (1. / 9007199254740992.);
}
double rotatedValueNoise(double gx, double gy, double invScale, double cosA, double sinA,
                         std::uint64_t seed, std::uint64_t octave) noexcept {
 const double u = (gx * cosA - gy * sinA) * invScale;
 const double v = (gx * sinA + gy * cosA) * invScale;
 const auto ix = std::int64_t(std::floor(u));
 const auto iy = std::int64_t(std::floor(v));
 const double fx = u - double(ix), fy = v - double(iy);
 const double sx = fx * fx * (3. - 2. * fx);
 const double sy = fy * fy * (3. - 2. * fy);
 const double n00 = hash2d(ix, iy, seed, octave);
 const double n10 = hash2d(ix + 1, iy, seed, octave);
 const double n01 = hash2d(ix, iy + 1, seed, octave);
 const double n11 = hash2d(ix + 1, iy + 1, seed, octave);
 const double nx0 = n00 + (n10 - n00) * sx;
 const double nx1 = n01 + (n11 - n01) * sx;
 return nx0 + (nx1 - nx0) * sy;
}
// Canvas-space multi-scale non-periodic paper tooth field in [0, 1].
// Uses rotated irrational-angle octaves so no horizontal/vertical grid lines appear,
// while remaining 100% tile-partition and segment-split invariant.
double paperTooth(double globalX, double globalY, std::uint64_t seed) noexcept {
 const double fine = rotatedValueNoise(globalX, globalY, 1. / 2.35, 0.79863551, 0.60181502, seed, 101);
 const double mid  = rotatedValueNoise(globalX, globalY, 1. / 6.80, 0.32556815, 0.94551858, seed, 202);
 const double coarse = rotatedValueNoise(globalX, globalY, 1. / 17.5, -0.45399050, 0.89100652, seed, 303);
 return std::clamp(0.46 * fine + 0.34 * mid + 0.20 * coarse, 0., 1.);
}
}
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
  const bool physical=hasPhysicalDryInk(e);
  auto unit=[&](std::int64_t fiber,std::int64_t node,std::uint64_t channel){
   std::uint64_t x=e.seed^(std::uint64_t(fiber)*0x9e3779b97f4a7c15ull)^(std::uint64_t(node)*0xbf58476d1ce4e5b9ull)^channel;
   x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;x^=x>>31;
   return double(x>>11)*(1./9007199254740992.);
  };
  auto segment=[&](double ax,double ay,double bx,double by,double pa,double pb,double arc,double tax,double tay,double tbx,double tby)->Result<void>{
   const double dx=bx-ax,dy=by-ay,length2=dx*dx+dy*dy;
   const double splayPad=physical?std::min(1.0,e.style.width*.08):0.0;
   const double pad=e.style.width*std::max(pa,pb)*.5+.5+splayPad;
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
     const double pressure=length2>0?pa+(pb-pa)*t:std::max(pa,pb);
     const double width=std::min(e.style.width,e.style.width*pressure+2.*splayPad*pressure);
     const double distance=std::hypot(px-(ax+dx*t),py-(ay+dy*t));
     if(distance>width*.5+.5+splayPad)continue;
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
  // Precompute arc-length curvature knots for physical bristle splay.
  // Colinear midpoint splits have zero turning angle, preserving exact split invariance.
  std::vector<std::pair<double,double>> curvatureKnots;
  if(physical && e.geometry.size()>=3){
   curvatureKnots.reserve(e.geometry.size());
   double acc=0;
   curvatureKnots.emplace_back(0.,0.);
   for(std::size_t i=1;i+1<e.geometry.size();++i){
    const double d0x=e.geometry[i].x-e.geometry[i-1].x,d0y=e.geometry[i].y-e.geometry[i-1].y;
    const double d1x=e.geometry[i+1].x-e.geometry[i].x,d1y=e.geometry[i+1].y-e.geometry[i].y;
    const double l0=std::hypot(d0x,d0y),l1=std::hypot(d1x,d1y);
    acc+=l0;
    double kappa=0;
    if(l0>1e-6 && l1>1e-6){
     const double dot=std::clamp((d0x*d1x+d0y*d1y)/(l0*l1),-1.,1.);
     const double angle=std::acos(dot);
     kappa=std::clamp(angle/std::max(1.,0.5*(l0+l1)),0.,1.5);
    }
    curvatureKnots.emplace_back(acc,kappa);
   }
   acc+=std::hypot(e.geometry.back().x-e.geometry[e.geometry.size()-2].x,
                   e.geometry.back().y-e.geometry[e.geometry.size()-2].y);
   curvatureKnots.emplace_back(acc,0.);
  }
  auto curvatureAt=[&](double s)->double{
   if(curvatureKnots.size()<2)return 0.;
   auto it=std::upper_bound(curvatureKnots.begin(),curvatureKnots.end(),s,
                            [](double val,const auto& kv){return val<kv.first;});
   if(it==curvatureKnots.begin())return it->second;
   if(it==curvatureKnots.end())return curvatureKnots.back().second;
   const auto prev=it-1;
   const double span=it->first-prev->first;
   if(span<=1e-9)return it->second;
   const double u=std::clamp((s-prev->first)/span,0.,1.);
   return prev->second+(it->second-prev->second)*u;
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
   arc+=std::hypot(b.x-a.x,b.y-a.y)*t;
   if(b.timestampUs>time)break;
  }
  const double totalArc=std::max(1.,arc);
  // Resolve texture once in the closest centerline frame.
  for(std::size_t i=0;i<nearest.size();i++){
   if((i%rect.width)==0 && stop.stop_requested())return error(ErrorCode::Cancelled,"dry-ink.cancelled");
   const auto& n=nearest[i];if(!std::isfinite(n.distance))continue;
   if(!physical){
    const double width=e.style.width*n.pressure;
    const double edge=std::clamp(width*.5+.5-n.distance,0.,std::min(1.,width));
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
    continue;
   }
   // Upgraded physical dry-ink model:
   // 1) World-space 2D multi-scale paper tooth (tile-seamless flying white / 飞白)
   // 2) Arc-length reservoir ink depletion with outer-bristle feathering
   // 3) Pressure & curvature-driven bristle clumping & splay down to fine widths
   const auto localX=std::int64_t(i%rect.width),localY=std::int64_t(i/rect.width);
   const double gx=double(rect.x+localX)+.5,gy=double(rect.y+localY)+.5;
   const double tooth=paperTooth(gx,gy,e.seed);
   const double kappa=curvatureAt(n.along);
   const double splay=1.0+0.12*std::clamp(n.pressure*0.5+kappa*1.4,0.,1.);
   const double width=std::min(e.style.width,e.style.width*n.pressure*splay);
   const double halfW=std::max(0.35,width*.5);
   const double outerFrac=std::clamp(std::abs(n.normal)/halfW,0.,1.2);
   // Reservoir ink depletes along arc length; outer bristles dry out before core bristles.
   const double decayLen=140.0+180.0*std::min(e.style.width,32.0);
   const double reservoir=0.34+0.66*std::exp(-n.along/decayLen);
   const double strokeProgress=std::clamp(n.along/totalArc,0.,1.);
   const double tailFeather=1.0-0.42*outerFrac*outerFrac*strokeProgress;
   const double inkLoad=std::clamp(reservoir*tailFeather,0.08,1.0);
   // Map normal coordinate into clumped bristle space (adaptive spacing works on fine <3px strokes too).
   const double basePitch=std::clamp(0.85+0.16*e.style.width,0.95,2.45);
   const double mappedNormal=n.normal/splay;
   const auto cell=std::int64_t(std::floor(mappedNormal/basePitch));
   const double nodePosition=n.along/(26.+0.8*e.style.width);
   const auto node=std::int64_t(std::floor(nodePosition));
   const double t=nodePosition-double(node),smooth=t*t*(3.-2.*t);
   if(6>32*1024*1024-work)return error(ErrorCode::BudgetExceeded,"dry-ink.work");
   work+=6;
   double bristleInk=0;
   for(auto bristle=cell-2;bristle<=cell+2;++bristle){
    // Clump neighboring bristles into irregular tufts that splay under pressure/curvature.
    const auto tuft=bristle>=0?(bristle/2):-((-bristle+1)/2);
    const double tuftCenter=(double(tuft*2)+1.0)*basePitch+(unit(tuft,0,13)-.5)*basePitch*0.9;
    const double rawCenter=(double(bristle)+.5)*basePitch+(unit(bristle,0,17)-.5)*basePitch*0.85;
    const double clumpMix=std::clamp(0.58-0.38*n.pressure-0.20*kappa,0.10,0.65);
    const double slowWave=(unit(bristle,node,29)*(1.-smooth)+unit(bristle,node+1,29)*smooth-.5)*basePitch*0.35;
    const double center=rawCenter*(1.-clumpMix)+tuftCenter*clumpMix+slowWave;
    const double supply=(unit(bristle,node,41)*(1.-smooth)+unit(bristle,node+1,41)*smooth)*inkLoad;
    const double radius=basePitch*(0.24+0.48*unit(bristle,0,71))*(0.42+0.78*n.pressure);
    const double overlap=std::max(0.,std::min(mappedNormal+.5*basePitch,center+radius)-
                                      std::max(mappedNormal-.5*basePitch,center-radius))/basePitch;
    // Paper valleys break ink first when local bristle supply or pressure drops ("飞白").
    const double valleyThreshold=std::clamp(0.76-0.54*supply-0.34*n.pressure+0.26*outerFrac*outerFrac,0.02,0.92);
    const double paperContact=std::clamp((tooth-valleyThreshold)/0.22,0.,1.);
    bristleInk+=overlap*paperContact*(0.35+0.65*supply);
   }
   bristleInk=std::clamp(bristleInk,0.,1.);
   // Organic ragged edge modulated by paper tooth rather than a clean geometric cylinder.
   const double raggedPad=0.55*(tooth-0.45)*(1.0-0.45*n.pressure);
   const double edge=std::clamp(width*.5+.5+raggedPad-n.distance,0.,std::min(1.,width+.35));
   const double textureMix=std::clamp((width-0.30)/1.35,0.22,1.0);
   const double alpha=edge*(0.22+0.78*std::pow(n.pressure,0.85))*((1.-textureMix)+textureMix*bristleInk);
   coverage[i]=std::uint16_t(std::clamp(std::lround(alpha*65535.),0l,65535l));
  }
  return coverage;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"dry-ink.allocation");}
}
}
