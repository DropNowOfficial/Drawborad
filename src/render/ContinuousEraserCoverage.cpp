#include "ContinuousEraserCoverage.h"
#include "FineLineCoverage.h"
#include "core/ContinuousEraserEvent.h"
namespace drawborad {
Result<std::vector<std::uint16_t>> continuousEraserCoverage(const StrokeEvent&e,Rect r,std::uint64_t time,std::stop_token stop) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"continuous-eraser.cancelled");
 auto valid=validateContinuousEraserEvent(e);if(!valid)return valid.error();
 // Pure coverage adapter; the original event is still engine22/eraser when
 // PixelBlend applies destination-out alpha to this layer only.
 auto round=e;round.engineVersion=19;round.toolId="smooth-ink-aa";
 return fineLineCoverage(round,r,time,stop);
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"continuous-eraser.allocation");}
}
