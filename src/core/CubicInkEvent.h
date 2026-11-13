#pragma once
#include "FreehandEvent.h"
#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stop_token>
namespace drawborad {
inline Result<void> validateCubicInkEvent(const StrokeEvent& e){
 if(e.engineVersion!=23||e.toolId!="cubic-ink-aa")return error(ErrorCode::UnsupportedFeature,"cubic.engine-tool");
 // Validate the style (including untrusted grain size) before any event copy.
 auto valid=validateRoundInkEvent(e,23,"cubic-ink-aa",128);if(!valid)return valid.error();
 if(e.geometry.size()!=4||e.seed!=0||e.startUs!=e.endUs||e.style.width<.5)return error(ErrorCode::InvalidInput,"cubic.canonical-controls");
 for(const auto& p:e.geometry)if(p.pressure!=1||p.tiltX||p.tiltY||p.timestampUs!=e.startUs)return error(ErrorCode::InvalidInput,"cubic.static-controls");
 return {};
}
struct CubicInkGeometry {std::vector<InputSample> samples;std::size_t visits{};};
namespace cubic_ink_detail {
struct Point {double x{},y{};};
inline Point blend(Point a,Point b,double t){return {a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t};}
inline double distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Node {std::array<Point,4> c;unsigned depth;};
// No new budget restriction on legacy-only documents. Checks are repeated on
// cache hits because a mutable alias may grow a non-cubic event's raw samples.
inline Result<bool> validateSourceLimits(const EventList& events,std::stop_token stop){
 bool hasCubic=false;
 for(const auto& e:events){if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");if(e.engineVersion==23)hasCubic=true;}
 if(!hasCubic)return false;
 if(events.size()>8192)return error(ErrorCode::BudgetExceeded,"cubic.event-budget");
 std::size_t raw=0,curves=0;
 for(const auto& e:events){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  if(e.geometry.size()>1000000-raw)return error(ErrorCode::BudgetExceeded,"cubic.raw-point-budget");
  raw+=e.geometry.size();
  if(e.engineVersion==23){if(++curves>256)return error(ErrorCode::BudgetExceeded,"cubic.curve-budget");auto valid=validateCubicInkEvent(e);if(!valid)return valid.error();}
 }
 return true;
}
inline Result<void> addExpandedBudget(std::size_t count,std::size_t work,std::size_t& points,std::size_t& visits){
 if(count>65536-points)return error(ErrorCode::BudgetExceeded,"cubic.expanded-budget");
 if(work>1048576-visits)return error(ErrorCode::BudgetExceeded,"cubic.total-work");
 points+=count;visits+=work;return {};
}
}
inline Result<CubicInkGeometry> prepareCubicInkGeometry(const StrokeEvent&e,std::stop_token stop={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateCubicInkEvent(e);if(!valid)return valid.error();
 using namespace cubic_ink_detail;
 std::array<Point,4> controls;double scale=1;
 for(std::size_t i=0;i<4;++i){controls[i]={e.geometry[i].x,e.geometry[i].y};scale=std::max({scale,std::abs(controls[i].x),std::abs(controls[i].y)});}
 // Conservative <=16-level floating error margin, not an interval proof.
 const double margin=scale*std::numeric_limits<double>::epsilon()*4096;
 std::array<Node,18> stack;std::size_t pending=1;stack[0]={controls,0};
 CubicInkGeometry out;out.samples.push_back(e.geometry.front());
 while(pending){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  if(++out.visits>8191)return error(ErrorCode::BudgetExceeded,"cubic.flatten-work");
  const auto n=stack[--pending];const auto& p=n.c;
  const double bound=std::max(distance(p[1],blend(p[0],p[3],1./3)),distance(p[2],blend(p[0],p[3],2./3)))+margin;
  if(bound<=1./16){
   if(out.samples.size()>=4096)return error(ErrorCode::BudgetExceeded,"cubic.flatten-points");
   auto sample=e.geometry.front();sample.x=p[3].x;sample.y=p[3].y;out.samples.push_back(sample);continue;
  }
  if(n.depth>=16||pending+2>stack.size())return error(ErrorCode::BudgetExceeded,"cubic.flatten-depth");
  const auto a=blend(p[0],p[1],.5),b=blend(p[1],p[2],.5),d=blend(p[2],p[3],.5),f=blend(a,b,.5),g=blend(b,d,.5),h=blend(f,g,.5);
  stack[pending++]={{h,g,d,p[3]},n.depth+1};stack[pending++]={{p[0],a,f,h},n.depth+1};
 }
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}
inline Result<void> validateCubicDocumentBudget(const EventList& events,std::stop_token stop={}){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto limits=cubic_ink_detail::validateSourceLimits(events,stop);if(!limits)return limits.error();if(!limits.value())return {};
 std::size_t points=0,visits=0;
 for(const auto& e:events)if(e.engineVersion==23){auto expanded=prepareCubicInkGeometry(e,stop);if(!expanded)return expanded.error();auto budget=cubic_ink_detail::addExpandedBudget(expanded.value().samples.size(),expanded.value().visits,points,visits);if(!budget)return budget.error();}
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 return {};
}
// Check an incoming authored curve before copying any legacy event history.
inline Result<void> validateCubicAppendBudget(const EventList& events,const StrokeEvent& incoming,std::stop_token stop={}){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateCubicInkEvent(incoming);if(!valid)return valid.error();
 if(events.size()>=8192)return error(ErrorCode::BudgetExceeded,"cubic.event-budget");
 std::size_t raw=4,curves=1,points=0,visits=0;
 for(const auto&e:events){if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");if(e.geometry.size()>1000000-raw)return error(ErrorCode::BudgetExceeded,"cubic.raw-point-budget");raw+=e.geometry.size();if(e.engineVersion==23&&++curves>256)return error(ErrorCode::BudgetExceeded,"cubic.curve-budget");}
 for(const auto&e:events)if(e.engineVersion==23){auto g=prepareCubicInkGeometry(e,stop);if(!g)return g.error();valid=cubic_ink_detail::addExpandedBudget(g.value().samples.size(),g.value().visits,points,visits);if(!valid)return valid.error();}
 auto g=prepareCubicInkGeometry(incoming,stop);if(!g)return g.error();return cubic_ink_detail::addExpandedBudget(g.value().samples.size(),g.value().visits,points,visits);
}

}
