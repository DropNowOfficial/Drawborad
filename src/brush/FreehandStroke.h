#pragma once
#include "core/Commands.h"
#include <span>
#include <stop_token>
namespace drawborad {
// Coordinates are canvas pixels; timestamps are monotone relative input times.
// Their intervals are retained, rebased strictly after the document's last event.
Result<Command> freehandStrokeCommand(const DocumentSnapshot&,NodeId layer,Id event,
    std::span<const InputSample> points,double width,ColorValue color,
    double opacity=1.0,std::stop_token={});
}
