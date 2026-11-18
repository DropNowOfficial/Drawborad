#pragma once
#include "core/Commands.h"
#include <QString>
#include <span>
#include <string>

namespace drawborad {

struct MyPaintDirectSample {
    double x{};
    double y{};
    double pressure{};
    double timeMs{};
};

bool mypaintDirectAvailable() noexcept;

Result<Command> mypaintDirectStrokeCommand(
    const DocumentSnapshot& snapshot,
    const std::string& presetName,
    std::span<const MyPaintDirectSample> points,
    std::array<std::uint8_t, 3> rgb,
    double radiusScale,
    bool normalMode,
    NodeId eventId);

} // namespace drawborad
