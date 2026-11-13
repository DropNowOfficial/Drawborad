#pragma once
#include "core/Document.h"
#include "core/CubicInkEvent.h"
#include <map>
#include <memory>
#include <stop_token>
namespace drawborad {
// Bounded per-event-list preparation for authored engine23/24 controls.
// Document/codec admission is separately validated; this cache grants no edit permission.
// Authored control copies stay immutable; only cached render surrogates use19.
// Concurrent mutation of input is unsupported. Sequential alias edits are detected.
struct PreparedCubicEvents {
 std::shared_ptr<const EventList> source;
 std::map<std::size_t,StrokeEvent> authoredEvents,renderEvents;
 std::size_t sourceEventCount{};
 std::size_t expandedPoints{};
};
class CubicInkPreparation {
 std::shared_ptr<const PreparedCubicEvents> cache_;
 public:
 Result<std::shared_ptr<const PreparedCubicEvents>> prepare(std::shared_ptr<const EventList>,std::stop_token={});
 std::shared_ptr<const PreparedCubicEvents> current()const{return cache_;}
};
inline Result<std::shared_ptr<const PreparedCubicEvents>> CubicInkPreparation::prepare(std::shared_ptr<const EventList> source,std::stop_token stop)try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 if(!source)return error(ErrorCode::InvalidInput,"cubic.source");
 auto limits=cubic_ink_detail::validateSourceLimits(*source,stop);if(!limits)return limits.error();
 if(cache_&&cache_->source==source&&cache_->sourceEventCount==source->size()){
  bool same=true;
  for(std::size_t i=0;i<source->size();++i){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
   const auto&e=source->at(i);auto found=cache_->authoredEvents.find(i);
   if(isAuthoredCubic(e)){if(found==cache_->authoredEvents.end()||e!=found->second){same=false;break;}}
   else if(found!=cache_->authoredEvents.end()){same=false;break;}
  }
  if(same)return cache_;
 }
 auto next=std::make_shared<PreparedCubicEvents>();next->source=source;next->sourceEventCount=source->size();
 std::size_t visits=0;
 for(std::size_t index=0;index<source->size();++index){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  const auto&e=source->at(index);if(!isAuthoredCubic(e))continue;
  auto geometry=prepareCubicForDocument(e,next->expandedPoints,visits,stop);if(!geometry)return geometry.error();
  StrokeEvent render=e;render.engineVersion=19;render.toolId="smooth-ink-aa";render.geometry=std::move(geometry.value().samples);
  next->authoredEvents.emplace(index,e);next->renderEvents.emplace(index,std::move(render));
 }
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 cache_=next;return cache_;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}
}
