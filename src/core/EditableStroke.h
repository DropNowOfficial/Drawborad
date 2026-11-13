#include "core/ChiselInkEvent.h"
#pragma once
#include "Document.h"
#include "FreehandEvent.h"
#include "DryInkEvent.h"
#include "view/LayerTree.h"
#include <stop_token>
namespace drawborad {
// Per-event native compatibility check, without revalidating the entire
// snapshot. Callers listing document events should start from a valid snapshot.
inline Result<void> independentNativeStroke(const DocumentSnapshot& s,const StrokeEvent& e,std::stop_token stop={}) {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
 if(e.engineVersion!=1&&e.engineVersion!=2&&e.engineVersion!=10&&e.engineVersion!=12&&e.engineVersion!=19&&e.engineVersion!=21)return error(ErrorCode::UnsupportedFeature,"stroke-edit.later-dependent-event");
 if(!s.nodes||!nativeLayerSupported(s,e.layerId)||!s.nodes->at(e.layerId).sourceResource.empty()||e.objectId!=s.nodes->at(e.layerId).objectId)return error(ErrorCode::UnsupportedFeature,"stroke-edit.later-nonnative");
 if(e.id.empty()||e.geometry.empty()||e.geometry.size()>1000000||e.color.profileId!=s.spec.colorSpec.profileId||e.color.activeChannels!=3||!std::isfinite(e.color.alpha)||e.color.alpha<0||e.color.alpha>1)return error(ErrorCode::InvalidInput,"stroke-edit.later-color-budget");
 for(auto c:e.color.channels)if(!std::isfinite(c)||c<0||c>1)return error(ErrorCode::InvalidInput,"stroke-edit.later-color");
 if(e.engineVersion==21)return validateChiselInkEvent(e);
 if(e.engineVersion==19)return validateSmoothInkEvent(e);
 if(e.engineVersion==12)return validateDryInkEvent(e);
 if(e.engineVersion==10)return validateFreehandEvent(e);
 if(e.toolId!="pencil"&&e.toolId!="colored-pencil"&&e.toolId!="fineliner"&&e.toolId!="marker"&&e.toolId!="highlight"&&e.toolId!="guide"&&e.toolId!="eraser")return error(ErrorCode::UnsupportedFeature,"stroke-edit.later-tool");
 const auto& style=e.style;
 if(!std::isfinite(style.width)||style.width<=0||style.width>8192||!std::isfinite(style.opacity)||style.opacity<0||style.opacity>1||!std::isfinite(style.hardness)||style.hardness<0||style.hardness>1||style.angle>7||(style.shape!=NibShape::Round&&style.shape!=NibShape::Chisel)||(!style.grain.empty()&&style.grain.size()!=64)||(e.engineVersion==1&&style!=StrokeStyle{}))return error(ErrorCode::InvalidInput,"stroke-edit.later-style");
 if(e.startUs>e.endUs||e.geometry.front().timestampUs!=e.startUs||e.geometry.back().timestampUs!=e.endUs)return error(ErrorCode::InvalidInput,"stroke-edit.later-time");
 auto last=e.startUs;
 for(const auto& p:e.geometry){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9||!std::isfinite(p.pressure)||p.pressure<0||p.pressure>1||p.timestampUs<last||p.timestampUs>e.endUs||(p.tiltX&&(!std::isfinite(*p.tiltX)||std::abs(*p.tiltX)>90))||(p.tiltY&&(!std::isfinite(*p.tiltY)||std::abs(*p.tiltY)>90)))return error(ErrorCode::InvalidInput,"stroke-edit.later-sample");last=p.timestampUs;}
 return {};
}
// Later hand-authored marks retain their exact geometry and are recomposited.
// Raster-derived fills/repairs are a dependency barrier, not silently retained
// after their source boundary is changed. This is compatibility, not provenance.
inline Result<std::size_t> editableStrokeIndex(const DocumentSnapshot& s,Id id,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(!w||!h||w>8192||h>8192||w*h>20000000||!s.nodes||!s.resources||!s.eventIndex||s.nodes->size()>4096||s.eventIndex->empty()||s.eventIndex->size()>8192)return error(ErrorCode::BudgetExceeded,"stroke-edit.document-budget");
 std::uint64_t count=0;std::size_t index=s.eventIndex->size();
 for(std::size_t i=0;i<s.eventIndex->size();++i){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
  const auto& e=(*s.eventIndex)[i];if(e.geometry.size()>1000000-count)return error(ErrorCode::BudgetExceeded,"stroke-edit.points");count+=e.geometry.size();if(e.id==id)index=i;}
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 if(index==s.eventIndex->size())return error(ErrorCode::InvalidInput,"stroke-edit.missing");
 const auto& selected=(*s.eventIndex)[index];
 if(selected.engineVersion!=19||selected.toolId!="smooth-ink-aa"||selected.geometry.size()>4096)return error(ErrorCode::InvalidInput,"stroke-edit.smooth-only");
 if(!nativeLayerPaintable(s,selected.layerId)||!s.nodes->at(selected.layerId).sourceResource.empty())return error(ErrorCode::Locked,"stroke-edit.layer-unavailable");
 auto selectedValid=independentNativeStroke(s,selected,stop);if(!selectedValid)return selectedValid.error();
 for(std::size_t i=index+1;i<s.eventIndex->size();++i){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
  const auto& e=(*s.eventIndex)[i];
  auto hand=independentNativeStroke(s,e,stop);if(!hand)return hand.error();
 }
 return index;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"stroke-edit.allocation");}
}
