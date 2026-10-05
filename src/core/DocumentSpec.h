#pragma once
#include "PixelFormat.h"
namespace drawborad {
struct NewDocumentRequest {
    Dimensions dimensions;
    ColorSpec colorSpec;
    bool transparent = true;
    ColorValue paperColor;
};
struct Background {
    bool transparent = true;
    ColorValue paperColor;
    bool operator==(const Background&) const = default;
};
struct DocumentSpec {
    PixelSize pixelSize;
    double ppi = 300;
    ColorSpec colorSpec;
    Background background;
    std::uint64_t estimatedBytes{}, tileCount{};
    bool operator==(const DocumentSpec&) const = default;
};
Result<void> validateDocumentSpec(const DocumentSpec&);
Result<PixelSize> toPixels(const Dimensions&);
Result<DocumentSpec> validateDocument(const NewDocumentRequest&, const ResourceBudget&);
Result<DocumentSpec> setResolution(const DocumentSpec&, double ppi);
} // namespace drawborad
