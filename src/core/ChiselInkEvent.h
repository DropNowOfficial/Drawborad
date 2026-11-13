#pragma once
#include "FreehandEvent.h"
namespace drawborad {
// Fixed 3:1 nib. Stored tilt remains input metadata, never rotates this engine.
inline Result<void> validateChiselInkEvent(const StrokeEvent&e){
 if(e.engineVersion!=21||e.toolId!="chisel-ink-aa")return error(ErrorCode::UnsupportedFeature,"chisel-ink.engine-tool");
 if(e.seed!=0||e.style.shape!=NibShape::Chisel||e.style.angle>7||!e.style.grain.empty()||e.style.width<.5)return error(ErrorCode::InvalidInput,"chisel-ink.style");
 const auto& s=e.style;
 if(!std::isfinite(s.width)||s.width<.5||s.width>128||!std::isfinite(s.opacity)||s.opacity<0||s.opacity>1||
    s.hardness!=1||s.shape!=NibShape::Chisel||s.angle>7||!s.grain.empty())return error(ErrorCode::InvalidInput,"chisel-ink.style");
 if(e.geometry.empty()||e.geometry.size()>4096)return error(ErrorCode::BudgetExceeded,"chisel-ink.points");
 if(e.startUs>e.endUs||e.geometry.front().timestampUs!=e.startUs||e.geometry.back().timestampUs!=e.endUs)
  return error(ErrorCode::InvalidInput,"chisel-ink.timeline");
 auto last=e.startUs;
 for(const auto& p:e.geometry){
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9||
     !std::isfinite(p.pressure)||p.pressure<0||p.pressure>1||p.timestampUs<last||p.timestampUs>e.endUs||
     (p.tiltX&&(!std::isfinite(*p.tiltX)||std::abs(*p.tiltX)>90))||
     (p.tiltY&&(!std::isfinite(*p.tiltY)||std::abs(*p.tiltY)>90)))return error(ErrorCode::InvalidInput,"chisel-ink.sample");
  last=p.timestampUs;
 }
 return {};
}
}
