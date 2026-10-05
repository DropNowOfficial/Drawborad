#pragma once
#include "Types.h"
namespace drawborad {
enum class Depth { U8 = 8, U16 = 16 };
enum class ColorMode { RGB, CMYK };
struct PixelFormat {
    ColorMode mode = ColorMode::RGB;
    Depth depth = Depth::U8;
    bool alpha = true;
    std::uint8_t channelCount() const {
        return std::uint8_t((mode == ColorMode::CMYK ? 4 : 3) + (alpha ? 1 : 0));
    }
    bool operator==(const PixelFormat&) const = default;
};
struct ColorSpec {
    PixelFormat format;
    ProfileId profileId;
    bool operator==(const ColorSpec&) const = default;
};
struct ColorValue {
    ProfileId profileId;
    std::array<double, 4> channels{};
    std::uint8_t activeChannels = 3;
    double alpha = 1;
    bool operator==(const ColorValue&) const = default;
};
struct TileData {
    PixelFormat format;
    std::uint32_t width{}, height{};
    std::uint64_t rowStride{};
    Bytes bytes;
    ProfileId profileId;
    std::uint32_t engineVersion = 1;
    bool operator==(const TileData&) const = default;
};
} // namespace drawborad
