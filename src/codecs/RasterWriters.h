#pragma once
#include "ExportService.h"
#include <array>

namespace drawborad {
struct MetadataPolicy {
    Bytes exif;
    Bytes xmp;
    bool preserveExif{};
    bool preserveXmp{};
    bool exifContainsGps{};
    bool allowGps{};
};

struct RasterEncodingOptions {
    Bytes iccProfile;
    double ppi{300.0};
    std::array<std::uint8_t,3> matte{255,255,255};
    int quality{90};
    MetadataPolicy metadata;
};

Result<Bytes> encodeRaster(ExportFormat, const TileData&, const RasterEncodingOptions&);
Result<ExportReport> writeRaster(const ExportPlan&, const TileData&,
                                 const RasterEncodingOptions&, std::stop_token = {});
}
