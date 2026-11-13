#pragma once
#include "core/Commands.h"
#include "view/LayerTree.h"
#include <cmath>
#include <stop_token>
namespace drawborad {
// Original arc-length locality, not sample count. Adjacent coincident samples
// share a distance/weight; no samples are inserted or removed. Out-of-range
// pressure or coordinates reject the entire proposal, never silently clamp.
inline Result<Command> localStrokeEditCommand(const DocumentSnapshot& s,Id id,
 std::size_t centerIndex,double radiusPixels,double dx,double dy,double pressureDelta,
 std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
 if(!std::isfinite(radiusPixels)||radiusPixels<=0||radiusPixels>128||!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>128||std::abs(dy)>128||!std::isfinite(pressureDelta)||std::abs(pressureDelta)>1)return error(ErrorCode::InvalidInput,"stroke-edit.parameters");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(!w||!h||w>8192||h>8192||w*h>20000000||!s.nodes||!s.eventIndex||s.eventIndex->empty()||s.eventIndex->size()>8192)return error(ErrorCode::BudgetExceeded,"stroke-edit.document-budget");
 std::uint64_t count=0;for(const auto& e:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");if(e.geometry.size()>1000000-count)return error(ErrorCode::BudgetExceeded,"stroke-edit.points");count+=e.geometry.size();}
 auto valid=validateSnapshot(s);if(!valid)return valid.error();const auto& e=s.eventIndex->back();
 if(e.id!=id||e.engineVersion!=19||e.toolId!="smooth-ink-aa"||e.geometry.size()>4096||centerIndex>=e.geometry.size())return error(ErrorCode::InvalidInput,"stroke-edit.last-smooth-only");
 if(!nativeLayerPaintable(s,e.layerId))return error(ErrorCode::Locked,"stroke-edit.layer-unavailable");
 auto geometry=e.geometry;
 auto adjust=[&](std::size_t index,double distance)->Result<void>{
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
  if(distance>=radiusPixels)return {};
  const double t=1-distance/radiusPixels,weight=t*t*(3-2*t);auto& p=geometry[index];
  p.x+=dx*weight;p.y+=dy*weight;p.pressure+=pressureDelta*weight;
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.pressure)||p.x<0||p.y<0||p.x>double(w)||p.y>double(h)||p.pressure<0||p.pressure>1)return error(ErrorCode::InvalidInput,"stroke-edit.result-bounds");
  return {};
 };
 auto done=adjust(centerIndex,0);if(!done)return done.error();double distance=0;
 for(std::size_t i=centerIndex;i>0;--i){distance+=std::hypot(e.geometry[i].x-e.geometry[i-1].x,e.geometry[i].y-e.geometry[i-1].y);if(distance>=radiusPixels)break;done=adjust(i-1,distance);if(!done)return done.error();}
 distance=0;for(std::size_t i=centerIndex+1;i<geometry.size();++i){distance+=std::hypot(e.geometry[i].x-e.geometry[i-1].x,e.geometry[i].y-e.geometry[i-1].y);if(distance>=radiusPixels)break;done=adjust(i,distance);if(!done)return done.error();}
 if(geometry==e.geometry)return error(ErrorCode::InvalidInput,"stroke-edit.no-change");
 return Command{AdjustStrokeSamples{id,std::move(geometry)}};
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"stroke-edit.allocation");}
}
