#include "CoverageRasterizer.h"
#include "storage/Binary.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace drawborad {
namespace {
std::uint16_t q(double v) {
    return std::uint16_t(std::clamp(std::llround(v * 65535.0), 0ll, 65535ll));
}
void writeSample(TileData& t, std::uint64_t at, std::uint16_t v) {
    if (t.format.depth == Depth::U8) t.bytes[std::size_t(at)] = std::uint8_t((std::uint32_t(v) + 128) / 257);
    else { t.bytes[std::size_t(at)] = std::uint8_t(v); t.bytes[std::size_t(at + 1)] = std::uint8_t(v >> 8); }
}
std::uint16_t readSampleImpl(const TileData& t, std::uint64_t at) {
    if (at >= t.bytes.size()) throw std::out_of_range("pixel sample");
    if (t.format.depth == Depth::U8) return std::uint16_t(t.bytes[std::size_t(at)] * 257u);
    if (at + 1 >= t.bytes.size()) throw std::out_of_range("pixel sample16");
    return std::uint16_t(t.bytes[std::size_t(at)] | (std::uint16_t(t.bytes[std::size_t(at + 1)]) << 8));
}
}
std::uint16_t readSample(const TileData& t, std::uint64_t at) { return readSampleImpl(t, at); }
void blendCoverage(TileData& t, const StrokeEvent& e, std::span<const std::uint16_t> coverage) {
    const unsigned colors = t.format.mode == ColorMode::CMYK ? 4 : 3;
    const unsigned sample = t.format.depth == Depth::U16 ? 2 : 1;
    const auto qa = q(e.color.alpha * e.style.opacity);
    std::array<std::uint16_t, 4> qc{}; for (unsigned i = 0; i < colors; i++) qc[i] = q(e.color.channels[i]);
    for (unsigned y = 0; y < t.height; y++) for (unsigned x = 0; x < t.width; x++) {
        auto sa = (std::uint64_t(coverage[std::size_t(y) * t.width + x]) * qa + 32767) / 65535;
        if (!sa) continue;
        auto at = std::uint64_t(y) * t.rowStride + std::uint64_t(x) * (colors + 1) * sample;
        auto da = readSampleImpl(t, at + colors * sample);
        if (e.toolId == "eraser") {
            auto oa = std::uint16_t((std::uint64_t(da) * (65535 - sa) + 32767) / 65535);
            writeSample(t, at + colors * sample, oa);
            if (!oa) for (unsigned c = 0; c < colors; c++) writeSample(t, at + c * sample, 0);
            continue;
        }
        auto den = sa * 65535 + std::uint64_t(da) * (65535 - sa);
        auto oa = std::uint16_t((den + 32767) / 65535);
        for (unsigned c = 0; c < colors; c++) {
            auto dc = readSampleImpl(t, at + c * sample);
            auto num = std::uint64_t(qc[c]) * sa * 65535 + std::uint64_t(dc) * da * (65535 - sa);
            writeSample(t, at + c * sample, std::uint16_t((num + den / 2) / den));
        }
        writeSample(t, at + colors * sample, oa);
    }
}
PixelHash canonicalPixelHash(const TileData& t) {
    binary::Writer w; w.text("DRAWBORAD-PIXELS-2"); w.u64(t.width); w.u64(t.height);
    w.u64(std::uint64_t(t.format.mode)); w.u64(std::uint64_t(t.format.depth)); w.boolean(t.format.alpha);
    w.hash(t.profileId); w.u64(t.engineVersion);
    const unsigned channels=t.format.channelCount(), sample=t.format.depth==Depth::U16?2:1;
    for(unsigned y=0;y<t.height;y++)for(unsigned x=0;x<t.width;x++){
        auto at=std::uint64_t(y)*t.rowStride+std::uint64_t(x)*channels*sample;
        bool transparent=t.format.alpha&&readSampleImpl(t,at+(channels-1)*sample)==0;
        for(unsigned c=0;c<channels;c++)w.u64(transparent?0:readSampleImpl(t,at+c*sample));
    }
    return sha256(w.data);
}
}
