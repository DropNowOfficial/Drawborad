#include "PixelBlend.h"
#include "CoverageRasterizer.h"
#include "core/ArtistOptIn.h"
#include "storage/Binary.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace drawborad {
namespace {
struct LinearSrgbTables {
    std::array<std::uint16_t, 256> toLinear{};
    std::array<std::uint8_t, 65536> toSrgb{};
    LinearSrgbTables() noexcept {
        for (unsigned i = 0; i < 256; ++i) {
            const double s = double(i) / 255.0;
            const double lin = s <= 0.04045 ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
            toLinear[i] = std::uint16_t(std::clamp(std::llround(lin * 65535.0), 0ll, 65535ll));
        }
        toLinear[0] = 0;
        toLinear[255] = 65535;
        for (unsigned i = 0; i < 65536; ++i) {
            const double lin = double(i) / 65535.0;
            const double s = lin <= 0.0031308 ? (lin * 12.92) : (1.055 * std::pow(lin, 1.0 / 2.4) - 0.055);
            toSrgb[i] = std::uint8_t(std::clamp(std::llround(s * 255.0), 0ll, 255ll));
        }
        for (unsigned i = 0; i < 256; ++i) {
            toSrgb[toLinear[i]] = std::uint8_t(i);
        }
    }
};
const LinearSrgbTables& lut() noexcept {
    static const LinearSrgbTables instance{};
    return instance;
}
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
std::uint16_t srgb8ToLinear16(std::uint8_t srgb) noexcept {
    return lut().toLinear[srgb];
}
std::uint8_t linear16ToSrgb8(std::uint16_t linear) noexcept {
    return lut().toSrgb[linear];
}
std::uint16_t srgbUnitToLinear16(double srgbUnit) noexcept {
    const double s = std::clamp(srgbUnit, 0.0, 1.0);
    const double scaled = s * 255.0;
    const auto lo = unsigned(std::floor(scaled));
    if (lo >= 255) return 65535;
    const double frac = scaled - double(lo);
    const auto a = lut().toLinear[lo];
    const auto b = lut().toLinear[lo + 1];
    return std::uint16_t(std::llround(double(a) + (double(b) - double(a)) * frac));
}
void compositeRgba8Pixel(std::uint8_t* dst, const std::uint8_t* src,
                         std::uint8_t opacity, BlendMode blend,
                         bool linearLight) noexcept {
    const auto sa = (unsigned(src[3]) * opacity + 127u) / 255u;
    if (!sa) return;
    const auto da = unsigned(dst[3]);
    const auto den = sa * 255u + da * (255u - sa);
    if (linearLight && blend == BlendMode::Normal) {
        const auto& table = lut();
        const std::uint64_t sa64 = sa, da64 = da, den64 = den;
        for (unsigned c = 0; c < 3; ++c) {
            const std::uint64_t cs = table.toLinear[src[c]];
            const std::uint64_t cb = table.toLinear[dst[c]];
            const auto num = cs * sa64 * 255ull + cb * da64 * (255ull - sa64);
            const auto lin = std::uint16_t((num + den64 / 2ull) / den64);
            dst[c] = table.toSrgb[lin];
        }
        dst[3] = std::uint8_t((den + 127u) / 255u);
        return;
    }
    const auto divisor = std::uint64_t(den) * 255ull;
    for (unsigned c = 0; c < 3; ++c) {
        const std::uint64_t cs = src[c], cb = dst[c];
        if (blend == BlendMode::Multiply) {
            const auto numerator = cs * sa * (255ull - da) * 255ull +
                                   cb * da * (255ull - sa) * 255ull +
                                   cs * cb * sa * da;
            dst[c] = std::uint8_t((numerator + divisor / 2ull) / divisor);
        } else if (blend == BlendMode::Screen) {
            const auto screened = 255ull * (cs + cb) - cs * cb;
            const auto numerator = cs * sa * (255ull - da) * 255ull +
                                   cb * da * (255ull - sa) * 255ull +
                                   screened * sa * da;
            dst[c] = std::uint8_t((numerator + divisor / 2ull) / divisor);
        } else {
            dst[c] = std::uint8_t((unsigned(cs) * sa * 255u +
                                   unsigned(cb) * da * (255u - sa) + den / 2u) / den);
        }
    }
    dst[3] = std::uint8_t((den + 127u) / 255u);
}
std::uint16_t readSample(const TileData& t, std::uint64_t at) { return readSampleImpl(t, at); }
void blendCoverage(TileData& t, const StrokeEvent& e, std::span<const std::uint16_t> coverage) {
    const unsigned colors = t.format.mode == ColorMode::CMYK ? 4 : 3;
    const unsigned sample = t.format.depth == Depth::U16 ? 2 : 1;
    const auto qa = q(e.color.alpha * e.style.opacity);
    const bool linearOptical = hasLinearOpticalBlend(e) &&
                               t.format.mode == ColorMode::RGB &&
                               t.format.depth == Depth::U8 &&
                               t.format.alpha &&
                               e.toolId != "eraser";
    if (linearOptical) {
        const auto& table = lut();
        std::array<std::uint16_t, 3> srcLin{
            srgbUnitToLinear16(e.color.channels[0]),
            srgbUnitToLinear16(e.color.channels[1]),
            srgbUnitToLinear16(e.color.channels[2])};
        for (unsigned y = 0; y < t.height; ++y) {
            for (unsigned x = 0; x < t.width; ++x) {
                const auto sa = (std::uint64_t(coverage[std::size_t(y) * t.width + x]) * qa + 32767ull) / 65535ull;
                if (!sa) continue;
                const auto at = std::size_t(std::uint64_t(y) * t.rowStride + std::uint64_t(x) * 4u);
                const std::uint64_t da = std::uint64_t(t.bytes[at + 3]) * 257ull;
                const auto den = sa * 65535ull + da * (65535ull - sa);
                const auto oa = std::uint16_t((den + 32767ull) / 65535ull);
                for (unsigned c = 0; c < 3; ++c) {
                    const std::uint64_t dc = table.toLinear[t.bytes[at + c]];
                    const auto num = std::uint64_t(srcLin[c]) * sa * 65535ull + dc * da * (65535ull - sa);
                    const auto outLin = std::uint16_t((num + den / 2ull) / den);
                    t.bytes[at + c] = table.toSrgb[outLin];
                }
                t.bytes[at + 3] = std::uint8_t((std::uint32_t(oa) + 128u) / 257u);
            }
        }
        return;
    }
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
