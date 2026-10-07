#pragma once
#include "PsdReader.h"

namespace drawborad {

struct PsdLayerPixels {
    std::string name;
    TileData pixels;
    std::uint8_t opacity{255};
    bool visible{true};
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
    Bytes iccProfile;
    double ppi{72.0};
    std::vector<PsdLayerPixels> layers;
    std::optional<TileData> merged;
    bool overwrite{};
};

struct PsdWriteStats {
    std::uint64_t stagedBytes{};
    std::uint64_t peakWriterBufferBytes{};
};

Result<Bytes> encodePsd(const PsdWriteRequest&);
Result<void> writePsd(const PsdWriteRequest&, std::stop_token = {}, PsdWriteStats* = nullptr);

} // namespace drawborad
