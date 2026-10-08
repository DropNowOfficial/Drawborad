#pragma once
#include "render/ReferenceRenderer.h"
#include <stop_token>
#include <string>
#include <vector>
namespace drawborad {
enum class CriticalKind { Lettering, Logo };
struct CriticalRegion {
    Rect bounds{};
    CriticalKind kind{CriticalKind::Lettering};
    std::string label;
    double maxColorMae{0.04};
    double maxAlphaMae{0.02};
    double maxEdgeMismatch{0.03};
};
struct CriticalRegionMetric {
    CriticalKind kind{CriticalKind::Lettering};
    std::string label;
    Rect bounds{};
    double colorMae{};
    double alphaMae{};
    double edgeMismatch{};
    std::uint64_t comparedPixels{};
    bool passed{};
};
struct CriticalRegionReport {
    std::vector<CriticalRegionMetric> regions;
    bool accepted{};
    bool editableTextClaimed{};
};
struct CriticalRegionRequest {
    DocumentSnapshot reconstructed;
    const TileData* source{};
    NodeId layerId{};
    Revision expectedRevision{};
    std::vector<CriticalRegion> regions;
};
Result<CriticalRegionReport> checkCriticalRegions(const CriticalRegionRequest&,
                                                  std::stop_token = {});
}
