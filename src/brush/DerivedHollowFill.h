#pragma once
#include "HollowDerivedReference.h"
namespace drawborad {
inline Result<Command> derivedHollowFillCommand(const DocumentSnapshot& s,
 NodeId source,NodeId overlay,NodeId colorLayer,Id event,std::uint32_t x,std::uint32_t y,
 ColorValue color,std::stop_token stop={}) try {
 if(event.empty()||colorLayer.empty())return error(ErrorCode::InvalidInput,"derived-fill.ids");
 auto ref=hollowDerivedReference(s,source,overlay,colorLayer,stop);if(!ref)return ref.error();
 for(const auto& e:*s.eventIndex)if(e.id==event)return error(ErrorCode::InvalidInput,"derived-fill.event-id");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 const bool existing=s.nodes->contains(colorLayer);
 if(x>=w||y>=h||!ref.value().cleared[std::size_t(y)*w+x])return error(ErrorCode::InvalidInput,"derived-fill.seed-outside-cover");
 Command out;
 if(!existing){LayerNode l;l.id=colorLayer;l.parent=Document::rootId();l.name="Hollow interior colors";l.bounds={0,0,w,h};out.operations.emplace_back(AddNode{l});}
 auto candidate=Document::fromSnapshot(s);if(!candidate)return candidate.error();
 if(!existing){auto added=candidate.value().apply(out,s.revision);if(!added)return added.error();}
 auto fill=bucketFillCommand(candidate.value().snapshot(),colorLayer,event,ref.value().reference,x,y,0,color,stop);if(!fill)return fill.error();
 for(auto& op:fill.value().operations)if(auto* paint=std::get_if<Paint>(&op)){paint->event.parentRevision=s.revision;auto safe=validateHollowColorDomain(paint->event,ref.value(),stop);if(!safe)return safe.error();out.operations.emplace_back(std::move(op));}
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"derived-fill.allocation");}
}
