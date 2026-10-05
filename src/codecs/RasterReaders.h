#pragma once
#include "ImageReader.h"

namespace drawborad {
Result<TileData> decodeRasterBytes(std::span<const std::uint8_t>, InputInfo&,
                                   const DecodeOptions&);
}
