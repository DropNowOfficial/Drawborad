#pragma once
#include "PsdReader.h"

namespace drawborad {

struct PsdLayerPixels {
    std::string name;
    TileData pixels;
    std::uint8_t opacity{255};
    BlendMode blend{BlendMode::Normal};
    Bytes mask;
    Rect maskBounds{};
    std::uint8_t maskDefault{255};
    std::optional<std::uint32_t> sectionType;
};

struct PsdWriteRequest {
    std::filesystem::path path;
    PixelSize size{};
    PixelFormat format{};
    ProfileId profile{};
    std::vector<PsdLayerPixels> layers;
    bool overwrite{};
};

Result<Bytes> encodePsd(const PsdWriteRequest&);
Result<void> writePsd(const PsdWriteRequest&, std::stop_token = {});

} // namespace drawborad
