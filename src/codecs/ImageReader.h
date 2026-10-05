#pragma once
#include "CodecLimits.h"
#include "core/Digest.h"
#include "core/PixelFormat.h"
#include <filesystem>
#include <stop_token>
#include <optional>
namespace drawborad {
using InputPath = std::filesystem::path;
enum class ImageFormat { Png, Jpeg, Webp, Tiff, Bmp, Psd, Psb, Heic, Unknown };
struct DecodeOptions { std::uint32_t frame{}; bool applyOrientation{true}; ProfileId explicitProfile{}; CodecLimits limits{}; };
struct InputInfo {
    PixelSize originalSize{}, orientedSize{};
    ImageFormat format{ImageFormat::Unknown};
    ProfileId icc{};
    std::vector<std::string> unsupported;
    std::uint64_t estimatedBytes{};
    Hash sourceHash{};
    bool hasAlpha{};
    std::uint16_t orientation{1};
};
struct ImportedImage { InputInfo info; Bytes encodedSource; std::optional<TileData> decodedPixels; };
Result<InputInfo> preflightInput(const InputPath&, const ResourceBudget&);
Result<ImportedImage> decodeImage(const InputPath&, const DecodeOptions&, std::stop_token = {});
}
