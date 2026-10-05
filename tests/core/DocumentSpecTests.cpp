#include "Test.h"
#include "core/DocumentSpec.h"
#include <cmath>
using namespace drawborad;
static NewDocumentRequest req(double w, double h) {
    NewDocumentRequest r;
    r.dimensions = {w, h, Unit::Px, 300};
    return r;
}
TEST("physical units have exact expected dimensions") {
    CHECK(toPixels({25.4, 25.4, Unit::Mm, 300}).value() == PixelSize{300, 300});
    CHECK(toPixels({2.54, 2.54, Unit::Cm, 300}).value() == PixelSize{300, 300});
    CHECK(toPixels({1, 2, Unit::In, 96}).value() == PixelSize{96, 192});
}
TEST("free aspect ratios and supported sizes") {
    for (auto sz :
         {PixelSize{1, 1}, PixelSize{4096, 4096}, PixelSize{8192, 8192}, PixelSize{3173, 911}}) {
        auto v = validateDocument(req(double(sz.width), double(sz.height)), {});
        CHECK(v);
        CHECK(v.value().pixelSize == sz);
    }
    CHECK(validateDocument(req(1, 1), {}).value().tileCount == 1);
    CHECK(validateDocument(req(4096, 4096), {}).value().tileCount == 64);
}
TEST("invalid dimensions rejected before allocation") {
    for (auto x : {0., -1., double(NAN), double(INFINITY)}) {
        auto v = toPixels({x, 2, Unit::Px, 300});
        CHECK(!v);
        CHECK(v.error().code == ErrorCode::InvalidInput);
    }
    CHECK(!toPixels({1, 1, Unit::In, 0}));
    CHECK(!toPixels({1, 1, static_cast<Unit>(90), 300}));
    CHECK(!toPixels({.01, .01, Unit::Px, 300}));
}
TEST("checked arithmetic and extreme sizes") {
    CHECK(!checkedMultiply(UINT64_MAX, 2));
    CHECK(!checkedAdd(UINT64_MAX, 1));
    CHECK(checkedMultiply(0, UINT64_MAX).value() == 0);
    auto v = validateDocument(req(1e15, 1e15), {});
    CHECK(!v);
    CHECK(v.error().code == ErrorCode::Overflow);
    CHECK(!toPixels({1e100, 1, Unit::Px, 300}));
}
TEST("real channel descriptors including CMYKA") {
    CHECK((PixelFormat{ColorMode::CMYK, Depth::U16, true}.channelCount() == 5));
    CHECK((PixelFormat{ColorMode::RGB, Depth::U8, false}.channelCount() == 3));
    auto r = req(512, 512);
    r.colorSpec.format = {ColorMode::CMYK, Depth::U16, true};
    auto v = validateDocument(r, {});
    CHECK(!v);
    CHECK(v.error().code == ErrorCode::ProfileRequired);
    r.colorSpec.profileId.bytes[0] = 1;
    v = validateDocument(r, {});
    CHECK(v);
    CHECK(v.value().estimatedBytes == 512 * 512 * 10);
    r.colorSpec.format.depth = static_cast<Depth>(32);
    CHECK(!validateDocument(r, {}));
}
TEST("budget enforced and PPI metadata separated from sampling") {
    ResourceBudget b;
    b.ramReclaimBytes = 1;
    b.scratchBytes = 1;
    auto v = validateDocument(req(1000, 1000), b);
    CHECK(!v);
    CHECK(v.error().code == ErrorCode::BudgetExceeded);
    auto spec = validateDocument(req(3173, 911), {}).value();
    auto m = setResolution(spec, 600);
    CHECK(m);
    CHECK(m.value().pixelSize == spec.pixelSize);
    CHECK(m.value().ppi == 600);
    CHECK(!setResolution(spec, NAN));
}
