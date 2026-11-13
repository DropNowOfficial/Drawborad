#pragma once
#include "Document.h"
#include <cmath>
namespace drawborad {
inline Result<void> validateRoundInkEvent(const StrokeEvent& e,std::uint32_t engine,const char* tool,double maxWidth){
 if(e.engineVersion!=engine || e.toolId!=tool)return error(ErrorCode::UnsupportedFeature,"freehand.engine-tool");
 const auto& s=e.style;
 if(!std::isfinite(s.width)||s.width<.05||s.width>maxWidth||!std::isfinite(s.opacity)||s.opacity<0||s.opacity>1||
    s.hardness!=1||s.shape!=NibShape::Round||s.angle!=0||!s.grain.empty())return error(ErrorCode::InvalidInput,"freehand.style");
 if(e.geometry.empty()||e.geometry.size()>4096)return error(ErrorCode::BudgetExceeded,"freehand.points");
 if(e.startUs>e.endUs||e.geometry.front().timestampUs!=e.startUs||e.geometry.back().timestampUs!=e.endUs)
  return error(ErrorCode::InvalidInput,"freehand.timeline");
 auto last=e.startUs;
 for(const auto& p:e.geometry){
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9||
     !std::isfinite(p.pressure)||p.pressure<0||p.pressure>1||p.timestampUs<last||p.timestampUs>e.endUs||
     (p.tiltX&&(!std::isfinite(*p.tiltX)||std::abs(*p.tiltX)>90))||
     (p.tiltY&&(!std::isfinite(*p.tiltY)||std::abs(*p.tiltY)>90)))return error(ErrorCode::InvalidInput,"freehand.sample");
  last=p.timestampUs;
 }
 return {};
}
inline Result<void> validateFreehandEvent(const StrokeEvent& e){return validateRoundInkEvent(e,10,"fineliner-aa",8);}
inline Result<void> validateSmoothInkEvent(const StrokeEvent& e){return validateRoundInkEvent(e,19,"smooth-ink-aa",128);}
}
