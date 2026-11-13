#pragma once
#include "EditableStroke.h"
#include "CubicInkEvent.h"
namespace drawborad {
inline Result<std::size_t> editableCubicIndex(const DocumentSnapshot&s,Id id,std::stop_token stop={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic-edit.cancelled");
 if(!s.nodes||!s.resources||!s.eventIndex||s.nodes->size()>4096||s.eventIndex->size()>8192)return error(ErrorCode::BudgetExceeded,"cubic-edit.document-budget");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 auto found=std::find_if(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto&e){return e.id==id;});
 if(found==s.eventIndex->end()||found->engineVersion!=23)return error(ErrorCode::InvalidInput,"cubic-edit.missing");
 if(!nativeLayerPaintable(s,found->layerId)||!s.nodes->at(found->layerId).sourceResource.empty())return error(ErrorCode::Locked,"cubic-edit.layer");
 for(const auto&o:s.fillOperations)if(o.source==found->layerId||o.target==found->layerId||o.boundaryLayer==found->layerId)return error(ErrorCode::UnsupportedFeature,"cubic-edit.recorded-dependency");
 const auto index=std::size_t(found-s.eventIndex->begin());
 for(std::size_t i=index+1;i<s.eventIndex->size();++i){if(s.eventIndex->at(i).engineVersion!=8){auto hand=independentNativeStroke(s,s.eventIndex->at(i),stop);if(!hand)return hand.error();}}
 auto barrier=validateDisjointBucketEdit(s,index,*found,stop);if(!barrier)return barrier.error();
 return index;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic-edit.allocation");}
inline bool cubicControlInDocumentRange(const DocumentSpec&spec,double x,double y){
 return std::isfinite(x)&&std::isfinite(y)&&x>=-double(spec.pixelSize.width)&&x<=2.*double(spec.pixelSize.width)&&y>=-double(spec.pixelSize.height)&&y<=2.*double(spec.pixelSize.height);
}
}
