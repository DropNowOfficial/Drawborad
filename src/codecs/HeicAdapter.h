#pragma once
#include "ImageReader.h"
namespace drawborad {
bool heicCodecAvailable();
bool looksLikeHeic(std::span<const std::uint8_t>);
Result<InputInfo> inspectHeic(std::span<const std::uint8_t>, const CodecLimits&);
Result<TileData> decodeHeic(std::span<const std::uint8_t>, InputInfo&, const DecodeOptions&,
                            std::stop_token = {});
}
