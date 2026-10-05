#pragma once
#include "RasterWriters.h"

namespace drawborad {
struct ArchiveLayer {
    std::string name;
    TileData pixels;
    std::int64_t x{}, y{};
    double opacity{1.0};
    BlendMode blend{BlendMode::Normal};
    bool visible{true};
};

Result<Bytes> encodeOra(PixelSize, std::span<const ArchiveLayer>, const TileData& merged,
                        const RasterEncodingOptions&);
Result<Bytes> encodeLayerZip(PixelSize, std::span<const ArchiveLayer>,
                             const RasterEncodingOptions&);
}
