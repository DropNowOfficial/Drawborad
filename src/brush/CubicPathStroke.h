#pragma once
#include "core/Commands.h"
#include "core/EditableCubicPath.h"
#include <algorithm>
namespace drawborad {
inline Result<Command> cubicPathStrokeCommand(const DocumentSnapshot&s,NodeId layer,Id id,const std::vector<std::array<double,2>>&controls,bool closed,double width,ColorValue color,double opacity=1,std::stop_token stop={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 if(!s.nodes||!s.resources||!s.eventIndex)return error(ErrorCode::CorruptData,"document.structure");
 if(controls.size()<(closed?3u:4u)||controls.size()>(closed?24u:25u)||(closed?controls.size()%3:(controls.size()-1)%3))return error(ErrorCode::BudgetExceeded,"cubic-path.control-count");
 StrokeEvent e;e.id=id;e.layerId=layer;e.parentRevision=s.revision;e.engineVersion=24;e.toolId=closed?"cubic-path-closed-aa":"cubic-path-open-aa";e.style.width=width;e.style.opacity=opacity;e.color=color;
 for(const auto&p:controls){if(!cubicControlInDocumentRange(s.spec,p[0],p[1]))return error(ErrorCode::InvalidInput,"cubic.control-bounds");e.geometry.push_back({p[0],p[1],1,{},{},0,0,0});}
 auto valid=validateCubicAppendBudget(*s.eventIndex,e,stop);if(!valid)return valid.error();
 valid=validateSnapshot(s);if(!valid)return valid.error();
 if(!nativeLayerPaintable(s,layer)||!s.nodes->at(layer).sourceResource.empty())return error(ErrorCode::UnsupportedFeature,"cubic.native-layer");
 if(id.empty()||std::any_of(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto&old){return old.id==id;}))return error(ErrorCode::InvalidInput,"cubic.event-id");
 std::uint64_t tick=0;for(const auto&old:*s.eventIndex){if(old.endUs==UINT64_MAX)return error(ErrorCode::Overflow,"cubic.time");tick=std::max(tick,old.endUs+1);}
 e.objectId=s.nodes->at(layer).objectId;e.startUs=e.endUs=tick;for(auto&p:e.geometry)p.timestampUs=tick;
 valid=validateCubicPathEvent(e);if(!valid)return valid.error();
 auto events=std::make_shared<EventList>(*s.eventIndex);events->push_back(e);auto candidate=s;candidate.eventIndex=events;valid=validateSnapshot(candidate);if(!valid)return valid.error();
 valid=validateCubicDocumentBudget(*events,stop);if(!valid)return valid.error();
 return Command(Paint{std::move(e),{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height}});
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}
inline Result<Command> adjustCubicPathControlCommand(const DocumentSnapshot&s,Id id,std::size_t index,double x,double y,std::stop_token stop={})try{
 auto selected=editableCubicPathIndex(s,id,stop);if(!selected)return selected.error();
 if(index>=s.eventIndex->at(selected.value()).geometry.size()||!cubicControlInDocumentRange(s.spec,x,y))return error(ErrorCode::InvalidInput,"cubic-path-edit.control-bounds");
 auto events=std::make_shared<EventList>(*s.eventIndex);events->at(selected.value()).geometry[index].x=x;events->at(selected.value()).geometry[index].y=y;
 auto valid=validateCubicDocumentBudget(*events,stop);if(!valid)return valid.error();
 return Command(AdjustCubicPathControl{id,index,x,y});
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic-path-edit.allocation");}
}
