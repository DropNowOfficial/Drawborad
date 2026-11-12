#pragma once
#include "Document.h"
#include <cmath>
namespace drawborad {
// v8 stores sorted, non-overlapping half-open horizontal pixel spans.
inline Result<void> validateBucketEvent(const StrokeEvent& e) {
    if (e.engineVersion!=8 || e.toolId!="bucket" || e.style!=StrokeStyle{} ||
        e.geometry.empty() || e.geometry.size()%2 || e.geometry.size()>131072 ||
        e.startUs!=e.endUs)
        return error(ErrorCode::InvalidInput,"bucket.event");
    double lastY=-1,lastEnd=-1;
    for (std::size_t i=0;i<e.geometry.size();i+=2) {
        const auto& a=e.geometry[i]; const auto& b=e.geometry[i+1];
        const auto valid=[&](const InputSample& p) {
            return std::isfinite(p.x) && std::isfinite(p.y) &&
                p.x>=0 && p.x<=UINT32_MAX && p.y>=0 && p.y<UINT32_MAX &&
                std::floor(p.x)==p.x && std::floor(p.y)==p.y &&
                p.pressure==1 && !p.tiltX && !p.tiltY && p.timestampUs==e.startUs;
        };
        if (!valid(a) || !valid(b) || a.y!=b.y || a.x>=b.x ||
            a.y<lastY || (a.y==lastY && a.x<lastEnd))
            return error(ErrorCode::InvalidInput,"bucket.runs");
        lastY=a.y;lastEnd=b.x;
    }
    return {};
}
}
