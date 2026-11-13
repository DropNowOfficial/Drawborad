#include "CubicInkStroke.h"
#include "core/EditableCubic.h"
#include <algorithm>
namespace drawborad {
Result<Command> cubicInkStrokeCommand(const DocumentSnapshot&s,NodeId layer,Id id,const std::array<std::array<double,2>,4>&controls,double width,ColorValue color,double opacity,std::stop_token stop)try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 if(!s.nodes||!s.resources||!s.eventIndex)return error(ErrorCode::CorruptData,"document.structure");
 StrokeEvent e;e.id=id;e.layerId=layer;e.parentRevision=s.revision;e.engineVersion=23;e.toolId="cubic-ink-aa";e.style.width=width;e.style.opacity=opacity;e.color=color;
 for(const auto&p:controls){if(!cubicControlInDocumentRange(s.spec,p[0],p[1]))return error(ErrorCode::InvalidInput,"cubic.control-bounds");e.geometry.push_back({p[0],p[1],1,{},{},0,0,0});}
 auto valid=validateCubicAppendBudget(*s.eventIndex,e,stop);if(!valid)return valid.error();
 valid=validateSnapshot(s);if(!valid)return valid.error();
 if(!nativeLayerPaintable(s,layer)||!s.nodes->at(layer).sourceResource.empty())return error(ErrorCode::UnsupportedFeature,"cubic.native-layer");
 if(id.empty()||std::any_of(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto&old){return old.id==id;}))return error(ErrorCode::InvalidInput,"cubic.event-id");
 std::uint64_t tick=0;for(const auto&old:*s.eventIndex){if(old.endUs==UINT64_MAX)return error(ErrorCode::Overflow,"cubic.time");tick=std::max(tick,old.endUs+1);}
 e.objectId=s.nodes->at(layer).objectId;e.startUs=e.endUs=tick;for(auto&p:e.geometry)p.timestampUs=tick;
 valid=validateCubicInkEvent(e);if(!valid)return valid.error();
 auto events=std::make_shared<EventList>(*s.eventIndex);events->push_back(e);auto candidate=s;candidate.eventIndex=events;valid=validateSnapshot(candidate);if(!valid)return valid.error();
 valid=validateCubicDocumentBudget(*events,stop);if(!valid)return valid.error();
 return Command(Paint{std::move(e),{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height}});
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}
Result<Command> adjustCubicControlsCommand(const DocumentSnapshot&s,Id id,const std::array<std::array<double,2>,4>&controls,std::stop_token stop)try{
 auto selected=editableCubicIndex(s,id,stop);if(!selected)return selected.error();
 auto events=std::make_shared<EventList>(*s.eventIndex);
 for(std::size_t i=0;i<4;++i){const auto&p=controls[i];if(!cubicControlInDocumentRange(s.spec,p[0],p[1]))return error(ErrorCode::InvalidInput,"cubic-edit.control-bounds");events->at(selected.value()).geometry[i].x=p[0];events->at(selected.value()).geometry[i].y=p[1];}
 auto valid=validateCubicDocumentBudget(*events,stop);if(!valid)return valid.error();
 return Command(AdjustCubicControls{id,controls});
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic-edit.allocation");}
}
