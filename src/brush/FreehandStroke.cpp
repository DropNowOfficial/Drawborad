#include "FreehandStroke.h"
#include "core/FreehandEvent.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
static Result<Command> roundInkStrokeCommand(const DocumentSnapshot& s,NodeId layer,Id event,
 std::span<const InputSample> points,double width,ColorValue color,double opacity,std::stop_token stop,bool broad) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"freehand.cancelled");
 if(!s.nodes||!s.resources||!s.eventIndex)return error(ErrorCode::CorruptData,"document.structure");
 if(s.nodes->size()>4096||s.eventIndex->size()>=8192||points.empty()||points.size()>4096)return error(ErrorCode::BudgetExceeded,"freehand.document-budget");
 std::uint64_t total=points.size(),tick=0;
 for(const auto& e:*s.eventIndex){
  if(e.geometry.size()>1000000-total)return error(ErrorCode::BudgetExceeded,"freehand.document-budget");
  total+=e.geometry.size();
  if(e.endUs==UINT64_MAX)return error(ErrorCode::Overflow,"freehand.timeline");
  tick=std::max(tick,e.endUs+1);
 }
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 if(!s.nodes->contains(layer)||s.nodes->at(layer).kind!=NodeKind::Layer)return error(ErrorCode::InvalidInput,"freehand.layer");
 if(layerLocked(s,layer))return error(ErrorCode::Locked,"freehand.locked");
 if(!nativeLayerPaintable(s,layer))return error(ErrorCode::UnsupportedFeature,"freehand.layer-unsupported");
 if(event.empty()||std::any_of(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto& e){return e.id==event;}))return error(ErrorCode::InvalidInput,"freehand.event-id");
 if(color.profileId!=s.spec.colorSpec.profileId||color.activeChannels!=3||!std::isfinite(color.alpha)||color.alpha<0||color.alpha>1)
  return error(ErrorCode::InvalidInput,"freehand.color");
 for(auto c:color.channels)if(!std::isfinite(c)||c<0||c>1)return error(ErrorCode::InvalidInput,"freehand.color");
 StrokeEvent e;e.id=event;e.layerId=layer;e.objectId=s.nodes->at(layer).objectId;e.parentRevision=s.revision;
 e.engineVersion=broad?19:10;e.toolId=broad?"smooth-ink-aa":"fineliner-aa";e.color=color;e.style.width=width;e.style.opacity=opacity;
 e.geometry.assign(points.begin(),points.end());e.startUs=points.front().timestampUs;e.endUs=points.back().timestampUs;
 valid=broad?validateSmoothInkEvent(e):validateFreehandEvent(e);if(!valid)return valid.error();
 const auto duration=e.endUs-e.startUs;
 if(duration>UINT64_MAX-tick)return error(ErrorCode::Overflow,"freehand.timeline");
 double x0=points.front().x,x1=x0,y0=points.front().y,y1=y0;
 for(auto& p:e.geometry){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"freehand.cancelled");
  if(p.x<0||p.y<0||p.x>s.spec.pixelSize.width||p.y>s.spec.pixelSize.height)return error(ErrorCode::InvalidInput,"freehand.outside-canvas");
  x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y);
  p.timestampUs=tick+(p.timestampUs-e.startUs);
 }
 e.startUs=tick;e.endUs=tick+duration;
 const auto left=std::max(0.,std::floor(x0-width/2-1)),top=std::max(0.,std::floor(y0-width/2-1));
 const auto right=std::min(double(s.spec.pixelSize.width),std::ceil(x1+width/2+1));
 const auto bottom=std::min(double(s.spec.pixelSize.height),std::ceil(y1+width/2+1));
 return Command(Paint{std::move(e),{std::int64_t(left),std::int64_t(top),std::uint64_t(right-left),std::uint64_t(bottom-top)}});
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"freehand.allocation");}
Result<Command> freehandStrokeCommand(const DocumentSnapshot& s,NodeId layer,Id event,
 std::span<const InputSample> points,double width,ColorValue color,double opacity,std::stop_token stop){
 return roundInkStrokeCommand(s,layer,event,points,width,color,opacity,stop,false);
}
Result<Command> smoothInkStrokeCommand(const DocumentSnapshot& s,NodeId layer,Id event,
 std::span<const InputSample> points,double width,ColorValue color,double opacity,std::stop_token stop){
 return roundInkStrokeCommand(s,layer,event,points,width,color,opacity,stop,true);
}
}
