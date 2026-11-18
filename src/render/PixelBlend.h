#pragma once
#include "core/Document.h"
#include <cstdint>

namespace drawborad {

// Zero-overhead LUT conversion between 8-bit sRGB [0..255] and 16-bit linear [0..65535].
std::uint16_t srgb8ToLinear16(std::uint8_t srgb) noexcept;
std::uint8_t linear16ToSrgb8(std::uint16_t linear) noexcept;
std::uint16_t srgbUnitToLinear16(double srgbUnit) noexcept;

// Straight-alpha RGBA8 layer pixel compositing supporting Normal, Multiply, Screen,
// and optional Linear-sRGB optical compositing.
void compositeRgba8Pixel(std::uint8_t* dst, const std::uint8_t* src,
                         std::uint8_t opacity, BlendMode blend,
                         bool linearLight = false) noexcept;

} // namespace drawborad
