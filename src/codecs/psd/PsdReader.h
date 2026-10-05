#pragma once
#include "codecs/ImageReader.h"
namespace drawborad {
struct PhotoshopLayerInfo { std::int32_t top{},left{},bottom{},right{}; std::uint16_t channels{}; std::uint8_t opacity{}; BlendMode blend{BlendMode::Normal}; };
struct PhotoshopInfo { ImageFormat format{ImageFormat::Unknown}; PixelSize size{}; std::uint16_t channels{},depth{},colorMode{}; std::vector<PhotoshopLayerInfo> layers; };
Result<PhotoshopInfo> readPhotoshopHeader(std::span<const std::uint8_t>, const CodecLimits&);
}
