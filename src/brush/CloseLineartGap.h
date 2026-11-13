#pragma once
#include "HollowDerivedReference.h"
namespace drawborad {
// Visible, user-directed short black correction. Never edits original resources
// or guesses a missing contour. Candidate validation proves compatible geometry,
// not who authored it. Caller may preview and then apply the atomic command.
inline Result<Command> closeLineartGapCommand(const DocumentSnapshot& s,NodeId source,NodeId overlay,Id event,
 std::uint32_t x0,std::uint32_t y0,std::uint32_t x1,std::uint32_t y1,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"gap.cancelled");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(!w||!h||w>8192||h>8192||w*h>20000000)return error(ErrorCode::BudgetExceeded,"gap.canvas-limit");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 if(source.empty()||overlay.empty()||event.empty()||source==overlay)return error(ErrorCode::InvalidInput,"gap.ids");
 auto path=lineartGapPixels({double(x0),double(y0)},{double(x1),double(y1)},std::uint32_t(w),std::uint32_t(h));if(!path)return path.error();
 const auto& root=s.nodes->at(Document::rootId());const bool existing=s.nodes->contains(overlay);
 if(!existing&&(root.children.size()!=2||root.children[1]!=source||s.nodes->size()!=3))return error(ErrorCode::UnsupportedFeature,"gap.initial-stack");
 if(existing){const auto color=root.children.size()==4?root.children[3]:NodeId{};auto current=hollowDerivedReference(s,source,overlay,color,stop);if(!current)return current.error();
  // Refuse exact duplicate barriers; crossing another black correction is safe.
  if(std::all_of(path.value().begin(),path.value().end(),[&](auto p){return std::binary_search(current.value().barriers.begin(),current.value().barriers.end(),p);}))return error(ErrorCode::InvalidInput,"gap.already-closed");
 }
 if(s.eventIndex->size()>=8192)return error(ErrorCode::BudgetExceeded,"gap.events");
 std::uint64_t tick=0,points=0;for(const auto& e:*s.eventIndex){if(e.id==event)return error(ErrorCode::InvalidInput,"gap.duplicate-event");points+=e.geometry.size();if(points>1000000||e.endUs==UINT64_MAX)return error(ErrorCode::BudgetExceeded,"gap.timeline");tick=std::max(tick,e.endUs+1);}
 StrokeEvent paint;paint.id=event;paint.layerId=overlay;paint.parentRevision=s.revision;paint.engineVersion=8;paint.toolId="bucket";paint.startUs=paint.endUs=tick;paint.color.activeChannels=3;paint.color.channels={0,0,0,0};paint.color.alpha=1;paint.color.profileId=s.spec.colorSpec.profileId;
 for(std::size_t i=0;i<path.value().size();){const auto p=path.value()[i];auto end=i+1;while(end<path.value().size()&&path.value()[end]/w==p/w&&path.value()[end]==path.value()[end-1]+1)++end;
  paint.geometry.push_back({double(p%w),double(p/w),1,{},{},tick,0,0});paint.geometry.push_back({double(path.value()[end-1]%w+1),double(p/w),1,{},{},tick,0,0});i=end;
 }
 if(points+paint.geometry.size()>1000000)return error(ErrorCode::BudgetExceeded,"gap.geometry");
 Command out;if(!existing){LayerNode layer;layer.id=overlay;layer.parent=Document::rootId();layer.name="Lineart corrections - white hollow / black gap barriers";layer.bounds={0,0,w,h};out.operations.emplace_back(AddNode{layer});}out.operations.emplace_back(Paint{std::move(paint),{0,0,w,h}});
 auto candidate=Document::fromSnapshot(s);if(!candidate)return candidate.error();auto applied=candidate.value().apply(out,s.revision);if(!applied)return applied.error();
 const auto next=candidate.value().snapshot();const auto& order=next.nodes->at(Document::rootId()).children;
 auto reference=hollowDerivedReference(next,source,overlay,order.size()==4?order[3]:NodeId{},stop);if(!reference)return reference.error();
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"gap.allocation");}
}
