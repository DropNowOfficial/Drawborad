#pragma once
#include "codecs/psd/PsdWriter.h"
#include "render/ReferenceRenderer.h"
namespace drawborad {
struct PaintedDocument {
    TileData merged;
    std::vector<PsdLayerPixels> layers;
    RenderStatistics statistics;
};
// No source image/model handle is accepted by this event-only render boundary.
Result<PaintedDocument> renderPaintedDocument(const DocumentSnapshot&,EventLimit,std::stop_token = {});
}
