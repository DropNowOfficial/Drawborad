#pragma once
#include "core/DocumentSpec.h"
#include "core/Result.h"
#include <filesystem>
#include <stop_token>
namespace drawborad {
using InputPath=std::filesystem::path;
enum class ImageFormat:std::uint8_t{PNG,JPEG,WebP,TIFF,BMP,PSD,PSB,HEIC,Unknown};
struct DecodeOptions{std::uint32_t frame{};bool applyOrientation{true};Hash explicitProfile{};std::uint64_t maxDecodedBytes{2ull*GiB};};
struct InputInfo{PixelSize originalSize{},orientedSize{};ImageFormat format{ImageFormat::Unknown};Hash icc{};std::uint64_t estimatedBytes{};Hash sourceHash{};std::vector<std::string> unsupported;};
struct ImportedImage{TileData pixels;InputInfo info;std::string provenance;};
Result<InputInfo> preflightInput(const InputPath&,const ResourceBudget&);
Result<ImportedImage> decodeImage(const InputPath&,const DecodeOptions&,std::stop_token={});
}