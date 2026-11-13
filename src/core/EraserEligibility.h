#pragma once
#include "EditableStroke.h"
namespace drawborad {
// Dependency check for a new eraser event, NOT permission to rewrite old events.
inline Result<void> continuousEraserLayerEligible(const DocumentSnapshot&s,NodeId layer,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"continuous-eraser.cancelled");
 if(!s.nodes||!s.resources||!s.eventIndex||s.nodes->size()>4096||s.eventIndex->size()>8192)return error(ErrorCode::BudgetExceeded,"continuous-eraser.document-budget");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 if(!nativeLayerPaintable(s,layer)||!s.nodes->at(layer).sourceResource.empty()||!s.nodes->at(layer).content.empty())return error(ErrorCode::UnsupportedFeature,"continuous-eraser.native-layer-required");
 for(const auto&o:s.fillOperations)if(o.boundaryLayer==layer||o.target==layer||o.source==layer)return error(ErrorCode::UnsupportedFeature,"continuous-eraser.recorded-fill-dependency");
 bool reached=false;std::uint64_t points=0;
 for(const auto&e:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"continuous-eraser.cancelled");if(e.geometry.size()>1000000-points)return error(ErrorCode::BudgetExceeded,"continuous-eraser.point-budget");points+=e.geometry.size();if(e.layerId==layer)reached=true;if(reached){auto independent=independentNativeStroke(s,e,stop);if(!independent){if(independent.error().code==ErrorCode::Cancelled)return independent.error();return error(ErrorCode::UnsupportedFeature,"continuous-eraser.later-dependent-event");}}}
 return {};
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"continuous-eraser.allocation");}
}
