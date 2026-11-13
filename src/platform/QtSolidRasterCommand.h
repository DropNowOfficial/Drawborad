#pragma once
#include "QtSolidDraftPreview.h"
#include "core/Commands.h"
namespace drawborad {
struct QtSolidRasterCandidate {
    Revision baseRevision{};
    Command command;
    Rect bounds;
    Hash pixelHash;
};
// Explicit bake to existing cropped Normal RGBA storage, NOT editable vector art.
// No mutation of source, no save, no implicit application to caller's document.
Result<QtSolidRasterCandidate> buildQtSolidRasterCommand(const DocumentSnapshot& source,
    Revision expectedRevision, const QtSolidMaskCache::Shape& shape,
    const ColorValue& color, double opacity, NodeId newLayer, std::stop_token stop = {});
}
