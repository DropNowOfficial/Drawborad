#pragma once
#include "Commands.h"
namespace drawborad {
inline constexpr char LineartStage[]="drawborad.lineart.rgba.v1";
Result<Command> makeLineartRasterCommand(Bytes rgba,Bytes originalPng,
    std::uint32_t width,std::uint32_t height,NodeId id);
// Borrowed immutable view; lifetime is the snapshot's resource lifetime.
Result<std::span<const std::uint8_t>> lineartRasterPixels(const DocumentSnapshot&,NodeId);
}
