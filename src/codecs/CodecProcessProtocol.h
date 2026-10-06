#pragma once
#include "ImageReader.h"
namespace drawborad {
struct CodecPayload {
    InputInfo info;
    TileData pixels;
};
Result<Bytes> encodeCodecPayload(const CodecPayload&, const CodecLimits&);
Result<CodecPayload> decodeCodecPayload(std::span<const std::uint8_t>, const CodecLimits&);
}
