#pragma once
#include "core/Commands.h"
#include <span>
#include <stop_token>
namespace drawborad {
// Fixed dry-ink recipe (hardness=1); width .25..128 canvas pixels.
// Seed is derived from all event UUID bytes and persisted for deterministic replay.
// Coordinates are canvas pixels; timestamps are monotone relative input times.
// Their intervals are retained, rebased strictly after the document's last event.
Result<Command> dryInkStrokeCommand(const DocumentSnapshot&,NodeId layer,Id event,
    std::span<const InputSample> points,double width,ColorValue color,
    double opacity=1.0,std::stop_token={});
}
