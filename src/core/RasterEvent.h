#include "core/CubicInkEvent.h"
#include "core/ContinuousEraserEvent.h"
#include "core/ChiselInkEvent.h"
#pragma once
#include "DryInkEvent.h"
#include "BucketEvent.h"
#include "FreehandEvent.h"
#include <cmath>
namespace drawborad {
inline Result<void> validateRasterEvent(const StrokeEvent& e) {
    if(e.engineVersion==23)return validateCubicInkEvent(e);
    if(e.engineVersion==22)return validateContinuousEraserEvent(e);
    if(e.engineVersion==21)return validateChiselInkEvent(e);
    if (e.engineVersion==8) return validateBucketEvent(e);
    if (e.engineVersion==12) return validateDryInkEvent(e);
    if (e.engineVersion != 1 && e.engineVersion != 2 && e.engineVersion != 10 && e.engineVersion != 19)
        return error(ErrorCode::UnsupportedFeature, "render.engine-version");
    if(e.engineVersion==10 || e.engineVersion==19){
        auto fine=e.engineVersion==19?validateSmoothInkEvent(e):validateFreehandEvent(e);if(!fine)return fine.error();
    }else if (e.toolId != "pencil" && e.toolId != "colored-pencil" && e.toolId != "fineliner" &&
        e.toolId != "marker" && e.toolId != "highlight" && e.toolId != "guide" &&
        e.toolId != "eraser")
        return error(ErrorCode::UnsupportedFeature, "render.tool");
    const auto& s = e.style;
    if (!std::isfinite(s.width) || s.width <= 0 || s.width > 8192 ||
        !std::isfinite(s.opacity) || s.opacity < 0 || s.opacity > 1 ||
        !std::isfinite(s.hardness) || s.hardness < 0 || s.hardness > 1 || s.angle > 7 ||
        (s.shape != NibShape::Round && s.shape != NibShape::Chisel) ||
        (!s.grain.empty() && s.grain.size() != 64))
        return error(ErrorCode::InvalidInput, "render.footprint");
    if (e.geometry.empty() || e.geometry.size() > 1000000)
        return error(ErrorCode::BudgetExceeded, "render.samples");
    for (const auto& p : e.geometry)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
            std::abs(p.y) > 1e9 || !std::isfinite(p.pressure) || p.pressure < 0 || p.pressure > 1)
            return error(ErrorCode::InvalidInput, "render.coordinate");
    return {};
}
}
