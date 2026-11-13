#pragma once
#include "LayerArchive.h"
#include "ZipStream.h"
#include <functional>
namespace drawborad {
// Budget models merged/current-layer/PNG capacities plus metadata and encoder
// scratch. Caller-owned snapshot and opaque provider caches need their own budget.
struct OraStreamLimits {
    std::uint64_t maxWorkingBytes=320*1024*1024, maxLayerBytes=128*1024*1024;
    std::uint64_t maxPngBytes=128*1024*1024, maxOutputBytes=2*1024*1024*1024ull;
    std::uint64_t maxMetadataBytes=4*1024*1024;
    std::size_t maxLayers=1024;
};
// Front-to-back order. Return one owned layer, not a retained collection.
// Pixels have masks baked in, but not layer opacity. Normal/Multiply only.
using OraLayerProvider=std::function<Result<ArchiveLayer>(std::size_t,std::stop_token)>;
Result<ExportReport> writeOraStream(const ExportPlan&,std::size_t layerCount,
    const OraLayerProvider&,const TileData& merged,const RasterEncodingOptions&,
    OraStreamLimits={},std::stop_token={});
}
