#pragma once
#include "codecs/ImageReader.h"
#include "core/Document.h"

namespace drawborad {

struct PhotoshopLayerInfo {
    std::int32_t top{}, left{}, bottom{}, right{};
    std::uint16_t channels{};
    std::uint8_t opacity{};
    BlendMode blend{BlendMode::Normal};
    std::string name;
    Rect maskBounds{};
    std::uint8_t maskDefault{255};
    bool hasUserMask{};
};

struct PhotoshopInfo {
    ImageFormat format{ImageFormat::Unknown};
    PixelSize size{};
    std::uint16_t channels{}, depth{}, colorMode{};
    std::vector<PhotoshopLayerInfo> layers;
};

struct PhotoshopDecodedLayer {
    PhotoshopLayerInfo info;
    TileData pixels;
    Bytes mask;
};

struct PhotoshopDecoded {
    PhotoshopInfo info;
    std::vector<PhotoshopDecodedLayer> layers;
};

Result<PhotoshopInfo> readPhotoshopHeader(std::span<const std::uint8_t>, const CodecLimits&);
Result<PhotoshopDecoded> decodePhotoshopLayers(std::span<const std::uint8_t>, ProfileId,
                                               const CodecLimits&);

} // namespace drawborad
