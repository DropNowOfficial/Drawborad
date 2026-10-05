#pragma once
#include "psd/PsdWriter.h"
namespace drawborad {
enum class ExportFormat { Psd, Png, Tiff, Jpeg, Webp, Ora, LayerZip };
struct ExportRequest { ExportFormat format{ExportFormat::Png}; std::filesystem::path path; PixelSize size{}; Depth depth{Depth::U8}; ProfileId profile{}; bool alpha{true}; bool overwrite{}; bool conversionConsent{}; bool matteChosen{}; };
struct ExportPlan { ExportRequest request; bool requiresRgbConversion{}; std::vector<std::string> conversionList; };
struct ExportReport { std::vector<std::filesystem::path> paths; std::vector<Hash> hashes; PixelSize dimensions{}; std::uint32_t channels{}; std::vector<std::string> warnings; };
Result<ExportPlan> preflightExport(ColorMode documentMode,const ExportRequest&);
Result<ExportReport> writeRasterPlaceholder(const ExportPlan&, std::span<const std::uint8_t> payload, std::stop_token = {});
}
