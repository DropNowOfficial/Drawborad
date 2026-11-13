#pragma once
#include "ui/MergedDocument.h"
#include <QImage>
namespace drawborad {
// Display-only composite: never replaces source layers or changes the document.
Result<QImage> renderCanvasFrame(const DocumentSnapshot&,std::uint64_t replayCount,
                                MergedBudget={},std::stop_token={});
}
