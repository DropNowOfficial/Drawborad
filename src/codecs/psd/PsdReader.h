#pragma once
#include "codecs/ImageReader.h"
#include "core/Document.h"
#include <optional>

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
    std::optional<std::uint32_t> sectionType;
};

struct PhotoshopInfo {
    ImageFormat format{ImageFormat::Unknown};
    PixelSize size{};
    std::uint16_t channels{}, depth{}, colorMode{};
    Bytes iccProfile;
    double ppi{};
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
    std::optional<TileData> composite;
};

Result<PhotoshopInfo> readPhotoshopHeader(std::span<const std::uint8_t>, const CodecLimits&);
Result<PhotoshopDecoded> decodePhotoshopLayers(std::span<const std::uint8_t>, ProfileId,
                                               const CodecLimits&);

} // namespace drawborad
