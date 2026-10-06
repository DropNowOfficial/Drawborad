#pragma once
#include "ImageReader.h"

namespace drawborad {
Result<void> applyExifOrientation(TileData&, std::uint16_t);
Result<TileData> decodeRasterBytes(std::span<const std::uint8_t>, InputInfo&,
                                   const DecodeOptions&);
}
