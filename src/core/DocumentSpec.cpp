#include "DocumentSpec.h"
#include <cmath>
namespace drawborad {
Result<std::uint64_t> checkedMultiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > UINT64_MAX / b)
        return error(ErrorCode::Overflow, "arithmetic.multiply");
    return a * b;
}
Result<std::uint64_t> checkedAdd(std::uint64_t a, std::uint64_t b) {
    if (a > UINT64_MAX - b)
        return error(ErrorCode::Overflow, "arithmetic.add");
    return a + b;
}
Result<PixelSize> toPixels(const Dimensions& d) {
    if (!std::isfinite(d.width) || !std::isfinite(d.height) || !std::isfinite(d.ppi) ||
        d.width <= 0 || d.height <= 0 || d.ppi <= 0)
        return error(ErrorCode::InvalidInput, "dimensions.positive-finite");
    long double f = 1;
    switch (d.unit) {
    case Unit::Px:
        break;
    case Unit::Mm:
        f = static_cast<long double>(d.ppi) / 25.4L;
        break;
    case Unit::Cm:
        f = static_cast<long double>(d.ppi) / 2.54L;
        break;
    case Unit::In:
        f = d.ppi;
        break;
    default:
        return error(ErrorCode::InvalidInput, "dimensions.unit");
    }
    auto one = [&](double x) -> Result<std::uint64_t> {
        const long double p = std::round(static_cast<long double>(x) * f);
        if (!std::isfinite(p) || p >= 18446744073709551616.L)
            return error(ErrorCode::Overflow, "dimensions.range");
        if (p < 1)
            return error(ErrorCode::InvalidInput, "dimensions.subpixel");
        return static_cast<std::uint64_t>(p);
    };
    auto w = one(d.width), h = one(d.height);
    if (!w)
        return w.error();
    if (!h)
        return h.error();
    return PixelSize{w.value(), h.value()};
}
Result<DocumentSpec> validateDocument(const NewDocumentRequest& r, const ResourceBudget& b) {
    auto sz = toPixels(r.dimensions);
    if (!sz)
        return sz.error();
    const auto f = r.colorSpec.format;
    if ((f.mode != ColorMode::RGB && f.mode != ColorMode::CMYK) ||
        (f.depth != Depth::U8 && f.depth != Depth::U16))
        return error(ErrorCode::InvalidInput, "color.format");
    if (f.mode == ColorMode::CMYK && r.colorSpec.profileId.empty())
        return error(ErrorCode::ProfileRequired, "color.cmyk-profile");
    if (sz.value().width > INT64_MAX || sz.value().height > INT64_MAX)
        return error(ErrorCode::Overflow, "dimensions.coordinates");
    auto count = checkedMultiply(sz.value().width, sz.value().height);
    if (!count)
        return count.error();
    auto bytes = checkedMultiply(count.value(),
                                 std::uint64_t(f.channelCount()) * (f.depth == Depth::U16 ? 2 : 1));
    if (!bytes)
        return bytes.error();
    auto budget = checkedAdd(b.ramReclaimBytes, b.scratchBytes);
    if (!budget)
        return budget.error();
    if (bytes.value() > budget.value())
        return error(ErrorCode::BudgetExceeded, "dimensions.storage-budget");
    auto ceiltiles = [](std::uint64_t n) { return n / 512 + (n % 512 != 0); };
    auto tiles = checkedMultiply(ceiltiles(sz.value().width), ceiltiles(sz.value().height));
    if (!tiles)
        return tiles.error();
    if (!r.transparent) {
        if (r.paperColor.activeChannels != (f.mode == ColorMode::CMYK ? 4 : 3) ||
            r.paperColor.profileId != r.colorSpec.profileId || !std::isfinite(r.paperColor.alpha) ||
            r.paperColor.alpha < 0 || r.paperColor.alpha > 1)
            return error(ErrorCode::InvalidInput, "color.paper");
        for (auto c : r.paperColor.channels)
            if (!std::isfinite(c) || c < 0 || c > 1)
                return error(ErrorCode::InvalidInput, "color.paper");
    }
    return DocumentSpec{sz.value(),    r.dimensions.ppi, r.colorSpec, {r.transparent, r.paperColor},
                        bytes.value(), tiles.value()};
}
Result<void> validateDocumentSpec(const DocumentSpec& d) {
    const auto& sz = d.pixelSize;
    const auto f = d.colorSpec.format;
    if (!sz.width || !sz.height || !std::isfinite(d.ppi) || d.ppi <= 0 ||
        (f.mode != ColorMode::RGB && f.mode != ColorMode::CMYK) ||
        (f.depth != Depth::U8 && f.depth != Depth::U16))
        return error(ErrorCode::InvalidInput, "document.spec");
    if (sz.width > INT64_MAX || sz.height > INT64_MAX)
        return error(ErrorCode::Overflow, "document.coordinates");
    if (f.mode == ColorMode::CMYK && d.colorSpec.profileId.empty())
        return error(ErrorCode::ProfileRequired, "color.cmyk-profile");
    auto pixels = checkedMultiply(sz.width, sz.height);
    if (!pixels)
        return pixels.error();
    auto bytes = checkedMultiply(pixels.value(),
                                 std::uint64_t(f.channelCount()) * (f.depth == Depth::U16 ? 2 : 1));
    if (!bytes)
        return bytes.error();
    auto tiles = checkedMultiply(sz.width / 512 + (sz.width % 512 != 0),
                                 sz.height / 512 + (sz.height % 512 != 0));
    if (!tiles)
        return tiles.error();
    if (d.estimatedBytes != bytes.value() || d.tileCount != tiles.value())
        return error(ErrorCode::CorruptData, "document.storage-estimate");
    const auto& c = d.background.paperColor;
    if (!d.background.transparent) {
        if (c.profileId != d.colorSpec.profileId ||
            c.activeChannels != (f.mode == ColorMode::CMYK ? 4 : 3) || !std::isfinite(c.alpha) ||
            c.alpha < 0 || c.alpha > 1)
            return error(ErrorCode::InvalidInput, "document.paper");
        for (auto x : c.channels)
            if (!std::isfinite(x) || x < 0 || x > 1)
                return error(ErrorCode::InvalidInput, "document.paper");
    }
    return {};
}
Result<DocumentSpec> setResolution(const DocumentSpec& d, double ppi) {
    if (!std::isfinite(ppi) || ppi <= 0)
        return error(ErrorCode::InvalidInput, "dimensions.ppi");
    auto out = d;
    out.ppi = ppi;
    return out;
}
} // namespace drawborad
