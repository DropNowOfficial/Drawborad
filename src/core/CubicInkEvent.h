#pragma once
#include "FreehandEvent.h"
#include "CubicPathEvent.h"
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
inline Result<CubicInkGeometry> prepareCubicInkGeometry(const StrokeEvent&e,std::stop_token stop={},CubicExpansionLimits limit={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateCubicInkEvent(e);
 if(!valid)return valid.error();
 limit.points=std::min<std::size_t>(limit.points,4096);limit.visits=std::min<std::size_t>(limit.visits,8191);
 CubicInkGeometry out;std::array<InputSample,4> controls;std::copy_n(e.geometry.begin(),4,controls.begin());
 auto done=appendCubicSegment(controls,out,limit,stop);
 if(!done)return done.error();
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}
inline bool isAuthoredCubic(const StrokeEvent&e){return e.engineVersion==23||e.engineVersion==24;}
inline Result<void> validateAuthoredCubic(const StrokeEvent&e){return e.engineVersion==24?validateCubicPathEvent(e):validateCubicInkEvent(e);}
inline std::size_t authoredCubicSegments(const StrokeEvent&e){return e.engineVersion==24?cubicPathSegmentCount(e):1;}
inline Result<CubicInkGeometry> prepareAuthoredCubic(const StrokeEvent&e,std::stop_token stop={},CubicExpansionLimits limit={}){return e.engineVersion==24?prepareCubicPathGeometry(e,stop,limit):prepareCubicInkGeometry(e,stop,limit);}
namespace cubic_ink_detail {
inline Result<bool> validateSourceLimits(const EventList& events,std::stop_token stop){
 bool hasCubic=false;for(const auto&e:events){if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 if(isAuthoredCubic(e))hasCubic=true;}
 if(!hasCubic)return false;
 if(events.size()>8192)return error(ErrorCode::BudgetExceeded,"cubic.event-budget");
 std::size_t raw=0,spans=0;
 for(const auto&e:events){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  if(e.geometry.size()>1000000-raw)return error(ErrorCode::BudgetExceeded,"cubic.raw-point-budget");
  raw+=e.geometry.size();
  if(isAuthoredCubic(e)){auto valid=validateAuthoredCubic(e);
 if(!valid)return valid.error();
 const auto count=authoredCubicSegments(e);
 if(count>256-spans)return error(ErrorCode::BudgetExceeded,"cubic.curve-budget");
 spans+=count;}
 }
 return true;
}
inline Result<void> addExpandedBudget(std::size_t count,std::size_t work,std::size_t& points,std::size_t& visits){
 if(count>65536-points)return error(ErrorCode::BudgetExceeded,"cubic.expanded-budget");
 if(work>1048576-visits)return error(ErrorCode::BudgetExceeded,"cubic.total-work");
 points+=count;visits+=work;
 return {};
}
}
inline Result<CubicInkGeometry> prepareCubicForDocument(const StrokeEvent&e,std::size_t&points,std::size_t&visits,std::stop_token stop={}){
 if(points>65536)return error(ErrorCode::BudgetExceeded,"cubic.expanded-budget");
 if(visits>1048576)return error(ErrorCode::BudgetExceeded,"cubic.total-work");
 const CubicExpansionLimits limit{std::min<std::size_t>(4096,65536-points),std::min<std::size_t>(8191,1048576-visits)};
 auto g=prepareAuthoredCubic(e,stop,limit);
 if(!g){if(g.error().messageKey=="cubic.flatten-points"&&limit.points<4096)return error(ErrorCode::BudgetExceeded,"cubic.expanded-budget");
 if(g.error().messageKey=="cubic.flatten-work"&&limit.visits<8191)return error(ErrorCode::BudgetExceeded,"cubic.total-work");
 return g.error();}
 auto valid=cubic_ink_detail::addExpandedBudget(g.value().samples.size(),g.value().visits,points,visits);
 if(!valid)return valid.error();
 return g;
}
inline Result<void> validateCubicDocumentBudget(const EventList& events,std::stop_token stop={}){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto limits=cubic_ink_detail::validateSourceLimits(events,stop);
 if(!limits)return limits.error();
 if(!limits.value())return {};
 std::size_t points=0,visits=0;for(const auto&e:events)if(isAuthoredCubic(e)){auto g=prepareCubicForDocument(e,points,visits,stop);
 if(!g)return g.error();}
 return {};
}
inline Result<void> validateCubicAppendBudget(const EventList&events,const StrokeEvent&incoming,std::stop_token stop={}){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateAuthoredCubic(incoming);
 if(!valid)return valid.error();
 if(events.size()>=8192)return error(ErrorCode::BudgetExceeded,"cubic.event-budget");
 std::size_t raw=incoming.geometry.size(),spans=authoredCubicSegments(incoming),points=0,visits=0;
 for(const auto&e:events){if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 if(e.geometry.size()>1000000-raw)return error(ErrorCode::BudgetExceeded,"cubic.raw-point-budget");
  raw+=e.geometry.size();
 if(isAuthoredCubic(e)){auto ok=validateAuthoredCubic(e);
 if(!ok)return ok.error();
 const auto count=authoredCubicSegments(e);
 if(count>256-spans)return error(ErrorCode::BudgetExceeded,"cubic.curve-budget");
 spans+=count;}}
 for(const auto&e:events)if(isAuthoredCubic(e)){auto g=prepareCubicForDocument(e,points,visits,stop);
 if(!g)return g.error();}
 auto g=prepareCubicForDocument(incoming,points,visits,stop);
 if(!g)return g.error();
 return {};
}
}
