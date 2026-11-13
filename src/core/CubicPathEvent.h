#pragma once
#include "FreehandEvent.h"
#include "CubicSubdivision.h"
#include <new>
namespace drawborad {
inline bool cubicPathClosed(const StrokeEvent&e){return e.toolId=="cubic-path-closed-aa";}
inline Result<void> validateCubicPathEvent(const StrokeEvent&e){
 const bool closed=cubicPathClosed(e);
 if(e.engineVersion!=24||(!closed&&e.toolId!="cubic-path-open-aa"))return error(ErrorCode::UnsupportedFeature,"cubic-path.engine-tool");
 const auto count=e.geometry.size();
 if(count<(closed?3u:4u)||count>(closed?24u:25u)||(closed?count%3:(count-1)%3))return error(ErrorCode::BudgetExceeded,"cubic-path.control-count");
 auto valid=validateRoundInkEvent(e,24,closed?"cubic-path-closed-aa":"cubic-path-open-aa",128);if(!valid)return valid.error();
 if(e.seed||e.startUs!=e.endUs||e.style.width<.5)return error(ErrorCode::InvalidInput,"cubic-path.canonical");
 for(const auto&p:e.geometry)if(p.pressure!=1||p.tiltX||p.tiltY||p.timestampUs!=e.startUs)return error(ErrorCode::InvalidInput,"cubic-path.static-controls");
 return {};
}
inline std::size_t cubicPathSegmentCount(const StrokeEvent&e){return cubicPathClosed(e)?e.geometry.size()/3:(e.geometry.size()-1)/3;}
inline Result<CubicInkGeometry> prepareCubicPathGeometry(const StrokeEvent&e,std::stop_token stop={},CubicExpansionLimits limit={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateCubicPathEvent(e);if(!valid)return valid.error();
 limit.points=std::min<std::size_t>(limit.points,4096);limit.visits=std::min<std::size_t>(limit.visits,8191);
 CubicInkGeometry out;const auto count=e.geometry.size();
 for(std::size_t i=0;i<cubicPathSegmentCount(e);++i){
  const std::array<InputSample,4> segment{e.geometry[3*i],e.geometry[3*i+1],e.geometry[3*i+2],e.geometry[(3*i+3)%count]};
  auto done=appendCubicSegment(segment,out,limit,stop);if(!done)return done.error();
 }
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic-path.allocation");}
}
