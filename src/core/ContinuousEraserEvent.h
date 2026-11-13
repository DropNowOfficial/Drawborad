#pragma once
#include "FreehandEvent.h"
#include <algorithm>
namespace drawborad {
inline Result<void> validateContinuousEraserEvent(const StrokeEvent&e){
 if(e.engineVersion!=22||e.toolId!="eraser")return error(ErrorCode::UnsupportedFeature,"continuous-eraser.engine-tool");
 if(e.seed!=0||e.style.width<.5||e.color.alpha!=1||e.color.activeChannels!=3||std::any_of(e.color.channels.begin(),e.color.channels.end(),[](double c){return c!=0;}))return error(ErrorCode::InvalidInput,"continuous-eraser.style-color");
 return validateRoundInkEvent(e,22,"eraser",128);
}
}
