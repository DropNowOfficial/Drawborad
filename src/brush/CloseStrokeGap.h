#pragma once
#include "core/Commands.h"
#include "core/EditableStroke.h"
#include "ClipPolygonSelection.h"
namespace drawborad {
// Explicit user action, never auto-snaps/moves samples. The appended endpoint
// is synthetic: copied first sample with the existing end timestamp.
inline Result<std::vector<InputSample>> closedStrokeGapGeometry(const DocumentSnapshot&s,Id id,std::stop_token stop={}) try {
 auto eligible=editableStrokeIndex(s,id,stop);if(!eligible)return eligible.error();
 std::uint64_t totalPoints=0;
 for(const auto&event:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-close.cancelled");if(event.geometry.size()>999999-totalPoints)return error(ErrorCode::BudgetExceeded,"stroke-close.document-point-budget");totalPoints+=event.geometry.size();}
 const auto&e=(*s.eventIndex)[eligible.value()];const auto&g=e.geometry;
 if(g.size()<3||g.size()>4095)return error(ErrorCode::BudgetExceeded,"stroke-close.sample-budget");
 for(const auto&o:s.fillOperations)if(o.boundaryEvent==id)return error(ErrorCode::UnsupportedFeature,"stroke-close.recorded-boundary");
 const double gap=std::hypot(g.back().x-g.front().x,g.back().y-g.front().y);
 if(gap==0)return error(ErrorCode::InvalidInput,"stroke-close.already-closed");
 double length=0;std::vector<DocumentPoint> polygon;polygon.reserve(std::min<std::size_t>(g.size(),256));
 for(std::size_t i=0;i<g.size();++i){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-close.cancelled");const auto&p=g[i];if(i)length+=std::hypot(p.x-g[i-1].x,p.y-g[i-1].y);if(polygon.empty()||polygon.back().x!=p.x||polygon.back().y!=p.y){if(polygon.size()>=256)return error(ErrorCode::BudgetExceeded,"stroke-close.vertex-budget");polygon.push_back({p.x,p.y});}}
 if(!std::isfinite(length)||!std::isfinite(gap)||gap>std::min(32.,length*.05))return error(ErrorCode::InvalidInput,"stroke-close.gap-too-large");
 auto simple=validateClipPolygon(polygon,std::uint32_t(s.spec.pixelSize.width),std::uint32_t(s.spec.pixelSize.height),stop);if(!simple)return simple.error();
 auto out=g;auto end=g.front();end.timestampUs=e.endUs;out.push_back(end);return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"stroke-close.allocation");}
inline Result<Command> closeStrokeGapCommand(const DocumentSnapshot&s,Id id,std::stop_token stop={}){auto checked=closedStrokeGapGeometry(s,id,stop);if(!checked)return checked.error();return Command{CloseStrokeGap{id}};}
}
