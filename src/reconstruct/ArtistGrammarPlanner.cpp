#include "ArtistGrammarPlanner.h"
#include "PlanningInternal.h"
#include "core/ArtistOptIn.h"
#include "render/CoverageRasterizer.h"
#include "render/PixelBlend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <vector>

namespace drawborad {
namespace {

struct LabPixel {
    float L{};
    float a{};
    float b{};
};

LabPixel srgbToLab(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) noexcept {
    const auto lin = [](std::uint8_t v) noexcept -> double {
        const double s = double(v) / 255.0;
        return s <= 0.04045 ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
    };
    const double r = lin(r8), g = lin(g8), b = lin(b8);
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b);
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
    const auto f = [](double t) noexcept -> double {
        return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0);
    };
    const double fx = f(x), fy = f(y), fz = f(z);
    return {float(116.0 * fy - 16.0), float(500.0 * (fx - fy)), float(200.0 * (fy - fz))};
}

double labDist2(const LabPixel& p, const LabPixel& q) noexcept {
    const double dL = double(p.L) - double(q.L);
    const double da = double(p.a) - double(q.a);
    const double db = double(p.b) - double(q.b);
    return dL * dL + da * da + db * db;
}

Id makeArtistEventId(NodeId objectId, NodeId layerId, std::uint64_t seed, std::uint32_t stageTag, std::uint64_t ordinal) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&](std::uint8_t x) noexcept { hash = (hash ^ x) * 1099511628211ull; };
    for (auto b : objectId.bytes) mix(b);
    for (auto b : layerId.bytes) mix(b);
    for (int i = 0; i < 8; ++i) mix(std::uint8_t(seed >> (8 * i)));
    mix(std::uint8_t(stageTag));
    Id id;
    for (int i = 0; i < 8; ++i) {
        id.bytes[std::size_t(i)] = std::uint8_t(hash >> (8 * (7 - i)));
        id.bytes[std::size_t(i + 8)] = std::uint8_t((ordinal + 1) >> (8 * (7 - i)));
    }
    return id;
}

struct EtfField {
    int width{};
    int height{};
    std::vector<float> tx;
    std::vector<float> ty;
    std::vector<float> mag;
};

EtfField computeLabEtf(const std::vector<LabPixel>& lab, int w, int h, std::stop_token stop) {
    EtfField etf{w, h, std::vector<float>(std::size_t(w * h), 1.0f),
                 std::vector<float>(std::size_t(w * h), 0.0f),
                 std::vector<float>(std::size_t(w * h), 0.0f)};
    const auto at = [&](int x, int y) noexcept -> const LabPixel& {
        const int cx = std::clamp(x, 0, w - 1);
        const int cy = std::clamp(y, 0, h - 1);
        return lab[std::size_t(cy) * std::size_t(w) + std::size_t(cx)];
    };
    std::vector<float> jxx(std::size_t(w * h), 0.0f);
    std::vector<float> jxy(std::size_t(w * h), 0.0f);
    std::vector<float> jyy(std::size_t(w * h), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& xm = at(x - 1, y); const auto& xp = at(x + 1, y);
            const auto& ym = at(x, y - 1); const auto& yp = at(x, y + 1);
            const auto& mm = at(x - 1, y - 1); const auto& mp = at(x - 1, y + 1);
            const auto& pm = at(x + 1, y - 1); const auto& pp = at(x + 1, y + 1);
            const auto sobel = [&](auto getter) noexcept -> std::pair<double, double> {
                const double gx = (getter(pm) + 2.0 * getter(xp) + getter(pp)) -
                                  (getter(mm) + 2.0 * getter(xm) + getter(mp));
                const double gy = (getter(mp) + 2.0 * getter(yp) + getter(pp)) -
                                  (getter(mm) + 2.0 * getter(ym) + getter(pm));
                return {gx * 0.125, gy * 0.125};
            };
            const auto [gLx, gLy] = sobel([](const LabPixel& p) { return double(p.L); });
            const auto [gax, gay] = sobel([](const LabPixel& p) { return double(p.a); });
            const auto [gbx, gby] = sobel([](const LabPixel& p) { return double(p.b); });
            const std::size_t idx = std::size_t(y) * std::size_t(w) + std::size_t(x);
            jxx[idx] = float(gLx * gLx + 0.7 * (gax * gax + gbx * gbx));
            jxy[idx] = float(gLx * gLy + 0.7 * (gax * gay + gbx * gby));
            jyy[idx] = float(gLy * gLy + 0.7 * (gay * gay + gby * gby));
        }
    }
    float maxMag = 1e-5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sxx = 0.0, sxy = 0.0, syy = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = std::clamp(x + dx, 0, w - 1);
                    const double kw = (dx == 0 && dy == 0) ? 4.0 : (dx == 0 || dy == 0 ? 2.0 : 1.0);
                    const std::size_t k = std::size_t(yy) * std::size_t(w) + std::size_t(xx);
                    sxx += kw * double(jxx[k]);
                    sxy += kw * double(jxy[k]);
                    syy += kw * double(jyy[k]);
                }
            }
            sxx /= 16.0; sxy /= 16.0; syy /= 16.0;
            const double trace = sxx + syy;
            const double diff = sxx - syy;
            const double disc = std::sqrt(diff * diff + 4.0 * sxy * sxy);
            const double lambda1 = 0.5 * (trace + disc);
            // Dominant gradient direction is eigenvector of lambda1; tangent is perpendicular.
            double gx = lambda1 - syy, gy = sxy;
            if (std::abs(gx) + std::abs(gy) < 1e-9) {
                gx = sxy; gy = lambda1 - sxx;
            }
            double tx = -gy, ty = gx;
            const double len = std::hypot(tx, ty);
            const std::size_t idx = std::size_t(y) * std::size_t(w) + std::size_t(x);
            if (len > 1e-8) {
                etf.tx[idx] = float(tx / len);
                etf.ty[idx] = float(ty / len);
            } else {
                etf.tx[idx] = 1.0f;
                etf.ty[idx] = 0.0f;
            }
            etf.mag[idx] = float(std::sqrt(std::max(0.0, lambda1)));
            maxMag = std::max(maxMag, etf.mag[idx]);
        }
    }
    for (auto& m : etf.mag) m /= maxMag;

    // 3 passes of Kang-Lee-Chui bilateral Edge Tangent Flow smoothing (radius 3).
    std::vector<float> nextTx(etf.tx.size()), nextTy(etf.ty.size());
    constexpr int R = 3;
    for (int iter = 0; iter < 3; ++iter) {
        if (stop.stop_requested()) break;
        for (int y = 0; y < h; ++y) {
            const int y0 = std::max(0, y - R), y1 = std::min(h - 1, y + R);
            for (int x = 0; x < w; ++x) {
                const int x0 = std::max(0, x - R), x1 = std::min(w - 1, x + R);
                const std::size_t cIdx = std::size_t(y) * std::size_t(w) + std::size_t(x);
                const double cx = etf.tx[cIdx], cy = etf.ty[cIdx], cm = etf.mag[cIdx];
                double accX = 0.0, accY = 0.0;
                for (int yy = y0; yy <= y1; ++yy) {
                    for (int xx = x0; xx <= x1; ++xx) {
                        const int d2 = (xx - x) * (xx - x) + (yy - y) * (yy - y);
                        if (d2 > R * R) continue;
                        const std::size_t qIdx = std::size_t(yy) * std::size_t(w) + std::size_t(xx);
                        const double qx = etf.tx[qIdx], qy = etf.ty[qIdx], qm = etf.mag[qIdx];
                        const double dot = cx * qx + cy * qy;
                        const double phi = dot >= 0.0 ? 1.0 : -1.0;
                        const double wm = 0.5 * (1.0 + std::tanh(3.5 * (qm - cm)));
                        const double wd = std::max(0.08, std::abs(dot));
                        const double wTotal = wm * wd;
                        accX += phi * wTotal * qx;
                        accY += phi * wTotal * qy;
                    }
                }
                const double len = std::hypot(accX, accY);
                if (len > 1e-7) {
                    nextTx[cIdx] = float(accX / len);
                    nextTy[cIdx] = float(accY / len);
                } else {
                    nextTx[cIdx] = etf.tx[cIdx];
                    nextTy[cIdx] = etf.ty[cIdx];
                }
            }
        }
        etf.tx.swap(nextTx);
        etf.ty.swap(nextTy);
    }
    return etf;
}

std::pair<double, double> sampleEtf(const EtfField& etf, double lx, double ly) noexcept {
    const double cx = std::clamp(lx - 0.5, 0.0, double(etf.width - 1));
    const double cy = std::clamp(ly - 0.5, 0.0, double(etf.height - 1));
    const int x0 = int(std::floor(cx)), y0 = int(std::floor(cy));
    const int x1 = std::min(etf.width - 1, x0 + 1), y1 = std::min(etf.height - 1, y0 + 1);
    const double fx = cx - double(x0), fy = cy - double(y0);
    const std::size_t i00 = std::size_t(y0) * std::size_t(etf.width) + std::size_t(x0);
    const std::size_t i10 = std::size_t(y0) * std::size_t(etf.width) + std::size_t(x1);
    const std::size_t i01 = std::size_t(y1) * std::size_t(etf.width) + std::size_t(x0);
    const std::size_t i11 = std::size_t(y1) * std::size_t(etf.width) + std::size_t(x1);
    const double rx = etf.tx[i00], ry = etf.ty[i00];
    const auto align = [&](std::size_t idx) noexcept -> std::pair<double, double> {
        const double vx = etf.tx[idx], vy = etf.ty[idx];
        return (vx * rx + vy * ry >= 0.0) ? std::pair{vx, vy} : std::pair{-vx, -vy};
    };
    const auto [x10v, y10v] = align(i10);
    const auto [x01v, y01v] = align(i01);
    const auto [x11v, y11v] = align(i11);
    const double vx = (1.0 - fy) * ((1.0 - fx) * rx + fx * x10v) + fy * ((1.0 - fx) * x01v + fx * x11v);
    const double vy = (1.0 - fy) * ((1.0 - fx) * ry + fy * 0.0 + fx * y10v) + fy * ((1.0 - fx) * y01v + fx * y11v);
    const double len = std::hypot(vx, vy);
    return len > 1e-8 ? std::pair{vx / len, vy / len} : std::pair{1.0, 0.0};
}

// Smooth bicubic Catmull-Rom level-set sampler for crisp 4x4 antialiased super-resolution silhouettes.
double sampleSmoothMask(std::span<const float> smoothField, int sw, int sh, double u, double v) noexcept {
    const double x = u - 0.5, y = v - 0.5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - double(ix), fy = y - double(iy);
    const auto cr = [](double p0, double p1, double p2, double p3, double t) noexcept -> double {
        return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
    };
    const auto fetch = [&](int px, int py) noexcept -> double {
        if (px < 0 || py < 0 || px >= sw || py >= sh) return 0.0;
        return double(smoothField[std::size_t(py) * std::size_t(sw) + std::size_t(px)]);
    };
    double col[4];
    for (int j = -1; j <= 2; ++j) {
        col[j + 1] = cr(fetch(ix - 1, iy + j), fetch(ix, iy + j), fetch(ix + 1, iy + j), fetch(ix + 2, iy + j), fx);
    }
    return cr(col[0], col[1], col[2], col[3], fy);
}

void appendBucketChunks(std::vector<StrokeEvent>& out,
                        NodeId objectId,
                        NodeId layerId,
                        Revision rev,
                        ColorValue color,
                        std::uint64_t seed,
                        std::uint32_t stageTag,
                        const std::vector<std::array<std::uint32_t, 3>>& spans,
                        std::uint64_t& timeUs,
                        std::uint32_t cellSide = 2048u) {
    if (spans.empty()) return;
    const std::uint32_t safeCell = std::max<std::uint32_t>(512u, cellSide);
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::array<std::uint32_t, 3>>> cells;
    for (const auto& [sx0, sx1, sy] : spans) {
        if (sx0 >= sx1) continue;
        const std::uint32_t cy = sy / safeCell;
        std::uint32_t curX = sx0;
        while (curX < sx1) {
            const std::uint32_t cx = curX / safeCell;
            const std::uint32_t nextX = std::min(sx1, (cx + 1) * safeCell);
            cells[{cy, cx}].push_back({curX, nextX, sy});
            curX = nextX;
        }
    }
    constexpr std::size_t MaxSpansPerEvent = 60000; // <= 120,000 samples (< 131,072 cap)
    for (const auto& [cellKey, cSpans] : cells) {
        (void)cellKey;
        for (std::size_t offset = 0; offset < cSpans.size(); offset += MaxSpansPerEvent) {
            const std::size_t end = std::min(cSpans.size(), offset + MaxSpansPerEvent);
            StrokeEvent e;
            e.id = makeArtistEventId(objectId, layerId, seed, stageTag, out.size());
            e.objectId = objectId;
            e.layerId = layerId;
            e.parentRevision = rev;
            e.engineVersion = 8;
            e.toolId = "bucket";
            e.color = color;
            e.seed = seed;
            e.style = StrokeStyle{};
            e.startUs = timeUs;
            e.endUs = timeUs;
            e.geometry.reserve((end - offset) * 2);
            for (std::size_t i = offset; i < end; ++i) {
                const auto [x0, x1, y] = cSpans[i];
                InputSample a{double(x0), double(y), 1.0, {}, {}, timeUs, 0, StrokeCapabilityLinearOptical};
                InputSample b{double(x1), double(y), 1.0, {}, {}, timeUs, 0, StrokeCapabilityLinearOptical};
                e.geometry.push_back(a);
                e.geometry.push_back(b);
            }
            timeUs += 50;
            out.push_back(std::move(e));
        }
    }
}

Result<void> rasterizeStrokeIntoRoi(TileData& roiCanvas,
                                    int roiX0,
                                    int roiY0,
                                    std::span<const std::uint8_t> roiMask,
                                    const StrokeEvent& event,
                                    std::stop_token stop) {
    auto bRes = strokeBounds(event);
    if (!bRes) return bRes.error();
    const auto b = bRes.value();
    const int x0 = std::max(roiX0, int(b.x));
    const int y0 = std::max(roiY0, int(b.y));
    const int x1 = std::min(roiX0 + int(roiCanvas.width), int(b.x + std::int64_t(b.width)));
    const int y1 = std::min(roiY0 + int(roiCanvas.height), int(b.y + std::int64_t(b.height)));
    if (x0 >= x1 || y0 >= y1) return {};
    for (int ty = y0; ty < y1; ty += int(TileSide)) {
        for (int tx = x0; tx < x1; tx += int(TileSide)) {
            const int tw = std::min(int(TileSide), x1 - tx);
            const int th = std::min(int(TileSide), y1 - ty);
            Rect box{tx, ty, std::uint64_t(tw), std::uint64_t(th)};
            auto cov = strokeCoverage(event, box, UINT64_MAX, stop);
            if (!cov) return cov.error();
            auto& cvec = cov.value();
            for (int py = 0; py < th; ++py) {
                const std::size_t maskRow = std::size_t(ty + py - roiY0) * std::size_t(roiCanvas.width);
                for (int px = 0; px < tw; ++px) {
                    if (!roiMask[maskRow + std::size_t(tx + px - roiX0)]) {
                        cvec[std::size_t(py) * std::size_t(tw) + std::size_t(px)] = 0;
                    }
                }
            }
            TileData patch;
            patch.width = std::uint32_t(tw);
            patch.height = std::uint32_t(th);
            patch.rowStride = std::uint64_t(tw) * 4;
            patch.format = roiCanvas.format;
            patch.profileId = roiCanvas.profileId;
            patch.bytes.resize(std::size_t(patch.rowStride) * std::size_t(th));
            for (int py = 0; py < th; ++py) {
                const auto srcOff = std::size_t(ty + py - roiY0) * roiCanvas.rowStride + std::size_t(tx - roiX0) * 4;
                std::copy_n(roiCanvas.bytes.data() + srcOff, patch.rowStride,
                            patch.bytes.data() + std::size_t(py) * patch.rowStride);
            }
            blendCoverage(patch, event, cvec);
            for (int py = 0; py < th; ++py) {
                const auto dstOff = std::size_t(ty + py - roiY0) * roiCanvas.rowStride + std::size_t(tx - roiX0) * 4;
                std::copy_n(patch.bytes.data() + std::size_t(py) * patch.rowStride, patch.rowStride,
                            roiCanvas.bytes.data() + dstOff);
            }
        }
    }
    return {};
}

StrokeEvent scaleContinuousEvent(StrokeEvent e,
                                 double scaleX,
                                 double scaleY,
                                 PixelSize targetSize,
                                 bool clipToTargetMask,
                                 std::span<const std::uint8_t> targetMask) {
    if (std::abs(scaleX - 1.0) < 1e-12 && std::abs(scaleY - 1.0) < 1e-12) return e;
    const double scaleW = std::sqrt(scaleX * scaleY);
    const double minW = e.engineVersion == 21 ? 0.5 : (e.engineVersion == 12 ? 0.25 : 0.05);
    const double maxW = e.engineVersion == 10 ? 8.0 : (e.engineVersion == 2 ? 64.0 : 128.0);
    e.style.width = std::clamp(e.style.width * scaleW, minW, maxW);
    const double maxX = std::nextafter(double(targetSize.width), 0.0);
    const double maxY = std::nextafter(double(targetSize.height), 0.0);
    std::vector<InputSample> validPts;
    validPts.reserve(e.geometry.size());
    for (auto p : e.geometry) {
        p.x = std::clamp(p.x * scaleX, 0.0, maxX);
        p.y = std::clamp(p.y * scaleY, 0.0, maxY);
        if (clipToTargetMask && !targetMask.empty()) {
            const auto ix = std::uint64_t(p.x), iy = std::uint64_t(p.y);
            if (!targetMask[std::size_t(iy * targetSize.width + ix)]) continue;
        }
        validPts.push_back(p);
    }
    e.geometry = std::move(validPts);
    if (!e.geometry.empty()) {
        e.startUs = e.geometry.front().timestampUs;
        e.endUs = e.geometry.back().timestampUs;
    }
    return e;
}

} // namespace

Result<TileData> inpaintAmodalBackground(const TileData& source,
                                         std::span<const std::uint8_t> unoccludedMask,
                                         std::stop_token stop) {
    if (stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.amodal-cancelled");
    const int w = int(source.width), h = int(source.height);
    if (w <= 0 || h <= 0 || source.format != PixelFormat{ColorMode::RGB, Depth::U8, true} ||
        unoccludedMask.size() != std::size_t(w) * std::size_t(h))
        return error(ErrorCode::InvalidInput, "artist.amodal-layout");

    // Erode unoccluded mask by 4px so foreground edge halos / uncertain segmentation rims never bleed into background.
    std::vector<std::uint8_t> seedMask(unoccludedMask.begin(), unoccludedMask.end());
    std::vector<std::uint8_t> eroded(seedMask.size(), 0);
    std::size_t erodedCount = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool ok = true;
            for (int dy = -4; dy <= 4 && ok; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                for (int dx = -4; dx <= 4; ++dx) {
                    if (dx * dx + dy * dy > 18) continue;
                    const int xx = std::clamp(x + dx, 0, w - 1);
                    if (!seedMask[std::size_t(yy) * std::size_t(w) + std::size_t(xx)]) {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok) {
                eroded[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 255;
                ++erodedCount;
            }
        }
    }
    if (erodedCount >= 16) seedMask.swap(eroded);

    struct PyrLevel {
        int w{}, h{};
        std::vector<std::array<float, 4>> rgbaW; // r, g, b, fixedSeedFlag (1.0 = known seed, 0.0 = occluded)
    };
    std::vector<PyrLevel> levels;
    PyrLevel base{w, h, std::vector<std::array<float, 4>>(std::size_t(w) * std::size_t(h))};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            if (seedMask[i]) {
                const std::size_t sAt = std::size_t(y) * source.rowStride + std::size_t(x) * 4;
                base.rgbaW[i] = {float(source.bytes[sAt]), float(source.bytes[sAt + 1]),
                                 float(source.bytes[sAt + 2]), 1.0f};
            } else {
                base.rgbaW[i] = {0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
    }
    levels.push_back(std::move(base));
    while (levels.back().w > 1 || levels.back().h > 1) {
        if (stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.amodal-cancelled");
        const auto& prev = levels.back();
        const int nw = std::max(1, (prev.w + 1) / 2);
        const int nh = std::max(1, (prev.h + 1) / 2);
        PyrLevel next{nw, nh, std::vector<std::array<float, 4>>(std::size_t(nw) * std::size_t(nh))};
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                std::array<float, 4> sum{0.0f, 0.0f, 0.0f, 0.0f};
                for (int dy = 0; dy < 2; ++dy) {
                    const int sy = std::min(prev.h - 1, y * 2 + dy);
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(prev.w - 1, x * 2 + dx);
                        const auto& v = prev.rgbaW[std::size_t(sy) * std::size_t(prev.w) + std::size_t(sx)];
                        if (v[3] > 0.5f) {
                            sum[0] += v[0]; sum[1] += v[1]; sum[2] += v[2]; sum[3] += 1.0f;
                        }
                    }
                }
                if (sum[3] > 0.5f) {
                    next.rgbaW[std::size_t(y) * std::size_t(nw) + std::size_t(x)] = {
                        sum[0] / sum[3], sum[1] / sum[3], sum[2] / sum[3], 1.0f};
                }
            }
        }
        levels.push_back(std::move(next));
    }
    if (levels.back().rgbaW[0][3] <= 0.5f) {
        levels.back().rgbaW[0] = {220.0f, 220.0f, 220.0f, 1.0f};
    }
    // Multi-grid Push + Gauss-Seidel Laplace relaxation at EVERY pyramid level for C-smooth harmonic completion!
    for (int lv = int(levels.size()) - 2; lv >= 0; --lv) {
        if (stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.amodal-cancelled");
        auto& fine = levels[std::size_t(lv)];
        const auto& coarse = levels[std::size_t(lv + 1)];
        for (int y = 0; y < fine.h; ++y) {
            const double cy = (double(y) + 0.5) * double(coarse.h) / double(fine.h) - 0.5;
            const int y0 = std::clamp(int(std::floor(cy)), 0, coarse.h - 1);
            const int y1 = std::clamp(y0 + 1, 0, coarse.h - 1);
            const float fy = float(std::clamp(cy - double(y0), 0.0, 1.0));
            for (int x = 0; x < fine.w; ++x) {
                const std::size_t idx = std::size_t(y) * std::size_t(fine.w) + std::size_t(x);
                if (fine.rgbaW[idx][3] > 0.5f) continue;
                const double cx = (double(x) + 0.5) * double(coarse.w) / double(fine.w) - 0.5;
                const int x0 = std::clamp(int(std::floor(cx)), 0, coarse.w - 1);
                const int x1 = std::clamp(x0 + 1, 0, coarse.w - 1);
                const float fx = float(std::clamp(cx - double(x0), 0.0, 1.0));
                const auto& c00 = coarse.rgbaW[std::size_t(y0) * std::size_t(coarse.w) + std::size_t(x0)];
                const auto& c10 = coarse.rgbaW[std::size_t(y0) * std::size_t(coarse.w) + std::size_t(x1)];
                const auto& c01 = coarse.rgbaW[std::size_t(y1) * std::size_t(coarse.w) + std::size_t(x0)];
                const auto& c11 = coarse.rgbaW[std::size_t(y1) * std::size_t(coarse.w) + std::size_t(x1)];
                for (std::size_t c = 0; c < 3; ++c) {
                    const float top = c00[c] * (1.0f - fx) + c10[c] * fx;
                    const float bot = c01[c] * (1.0f - fx) + c11[c] * fx;
                    fine.rgbaW[idx][c] = top * (1.0f - fy) + bot * fy;
                }
                // Keep fine.rgbaW[idx][3] == 0.0f so Gauss-Seidel relaxes all occluded pixels at this scale!
            }
        }
        const int gsIters = lv == 0 ? 20 : 32;
        for (int iter = 0; iter < gsIters; ++iter) {
            for (int y = 0; y < fine.h; ++y) {
                const int ym = std::max(0, y - 1), yp = std::min(fine.h - 1, y + 1);
                for (int x = 0; x < fine.w; ++x) {
                    const std::size_t idx = std::size_t(y) * std::size_t(fine.w) + std::size_t(x);
                    if (fine.rgbaW[idx][3] > 0.5f) continue;
                    const auto& l = fine.rgbaW[std::size_t(y) * std::size_t(fine.w) + std::size_t(std::max(0, x - 1))];
                    const auto& r = fine.rgbaW[std::size_t(y) * std::size_t(fine.w) + std::size_t(std::min(fine.w - 1, x + 1))];
                    const auto& u = fine.rgbaW[std::size_t(ym) * std::size_t(fine.w) + std::size_t(x)];
                    const auto& d = fine.rgbaW[std::size_t(yp) * std::size_t(fine.w) + std::size_t(x)];
                    for (std::size_t c = 0; c < 3; ++c) {
                        const float avg = 0.25f * (l[c] + r[c] + u[c] + d[c]);
                        fine.rgbaW[idx][c] += 1.35f * (avg - fine.rgbaW[idx][c]);
                    }
                }
            }
        }
    }
    const auto& full = levels.front().rgbaW;
    TileData out = source;
    out.rowStride = std::uint64_t(w) * 4;
    out.bytes.resize(std::size_t(out.rowStride) * std::size_t(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            const std::size_t dst = std::size_t(y) * out.rowStride + std::size_t(x) * 4;
            if (seedMask[i]) {
                const std::size_t srcAt = std::size_t(y) * source.rowStride + std::size_t(x) * 4;
                std::copy_n(source.bytes.data() + srcAt, 4, out.bytes.data() + dst);
            } else {
                out.bytes[dst + 0] = std::uint8_t(std::clamp(int(std::lround(full[i][0])), 0, 255));
                out.bytes[dst + 1] = std::uint8_t(std::clamp(int(std::lround(full[i][1])), 0, 255));
                out.bytes[dst + 2] = std::uint8_t(std::clamp(int(std::lround(full[i][2])), 0, 255));
                out.bytes[dst + 3] = 255;
            }
        }
    }
    return out;
}

Result<ArtistObjectPlan> planArtistObject(const ArtistObjectRequest& req) {
    if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
    if (!req.source || req.objectId.empty() || req.layers.flats.empty() ||
        req.source->format != PixelFormat{ColorMode::RGB, Depth::U8, true})
        return error(ErrorCode::InvalidInput, "artist.request-layout");

    const auto& src = *req.source;
    const int sw = int(src.width), sh = int(src.height);
    if (sw <= 0 || sh <= 0 || req.sourceMask.size() != std::size_t(sw) * std::size_t(sh))
        return error(ErrorCode::InvalidInput, "artist.mask-layout");

    const PixelSize targetSize = (req.targetSize.width && req.targetSize.height)
                                     ? req.targetSize
                                     : req.snapshot.spec.pixelSize;
    if (!targetSize.width || !targetSize.height || targetSize.width > 8192 ||
        targetSize.height > 8192 || targetSize.width * targetSize.height > 20000000ull)
        return error(ErrorCode::BudgetExceeded, "artist.target-canvas");

    const double scaleX = double(targetSize.width) / double(sw);
    const double scaleY = double(targetSize.height) / double(sh);
    const bool isSuperRes = (targetSize.width != src.width || targetSize.height != src.height);

    // Find tight ROI bounding box of the object in source space.
    int rx0 = sw, ry0 = sh, rx1 = 0, ry1 = 0;
    std::size_t activePixels = 0;
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            if (req.sourceMask[std::size_t(y) * std::size_t(sw) + std::size_t(x)]) {
                rx0 = std::min(rx0, x); ry0 = std::min(ry0, y);
                rx1 = std::max(rx1, x + 1); ry1 = std::max(ry1, y + 1);
                ++activePixels;
            }
        }
    }
    if (activePixels == 0 || rx0 >= rx1 || ry0 >= ry1)
        return error(ErrorCode::InvalidInput, "reconstruct.empty-safe-region");

    rx0 = std::max(0, rx0 - 6); ry0 = std::max(0, ry0 - 6);
    rx1 = std::min(sw, rx1 + 6); ry1 = std::min(sh, ry1 + 6);
    const int rw = rx1 - rx0, rh = ry1 - ry0;

    std::vector<std::uint8_t> roiMask(std::size_t(rw) * std::size_t(rh), 0);
    std::vector<LabPixel> roiLab(std::size_t(rw) * std::size_t(rh));
    std::vector<std::array<float, 3>> smoothRgb(std::size_t(rw) * std::size_t(rh));
    for (int ly = 0; ly < rh; ++ly) {
        const int gy = ry0 + ly;
        for (int lx = 0; lx < rw; ++lx) {
            const int gx = rx0 + lx;
            const std::size_t li = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
            roiMask[li] = req.sourceMask[std::size_t(gy) * std::size_t(sw) + std::size_t(gx)];
            const std::size_t sAt = std::size_t(gy) * src.rowStride + std::size_t(gx) * 4;
            roiLab[li] = srgbToLab(src.bytes[sAt], src.bytes[sAt + 1], src.bytes[sAt + 2]);
            smoothRgb[li] = {float(src.bytes[sAt]), float(src.bytes[sAt + 1]), float(src.bytes[sAt + 2])};
        }
    }

    // Edge-preserving bilateral filter on ROI RGB + Lab for clean flat zones & glaze targets.
    std::vector<std::array<float, 3>> bilatRgb = smoothRgb;
    std::vector<LabPixel> bilatLab = roiLab;
    for (int pass = 0; pass < 2; ++pass) {
        if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
        std::vector<std::array<float, 3>> nextRgb = bilatRgb;
        for (int ly = 0; ly < rh; ++ly) {
            for (int lx = 0; lx < rw; ++lx) {
                const std::size_t cIdx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!roiMask[cIdx]) continue;
                const auto& cLab = bilatLab[cIdx];
                double wr = 0.0, wg = 0.0, wb = 0.0, wSum = 0.0;
                for (int dy = -2; dy <= 2; ++dy) {
                    const int yy = std::clamp(ly + dy, 0, rh - 1);
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = std::clamp(lx + dx, 0, rw - 1);
                        const std::size_t qIdx = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                        if (!roiMask[qIdx]) continue;
                        const double d2 = labDist2(cLab, bilatLab[qIdx]);
                        const double sp = double(dx * dx + dy * dy);
                        const double w = std::exp(-sp / 4.5 - d2 / 196.0);
                        wr += w * double(bilatRgb[qIdx][0]);
                        wg += w * double(bilatRgb[qIdx][1]);
                        wb += w * double(bilatRgb[qIdx][2]);
                        wSum += w;
                    }
                }
                if (wSum > 1e-8) {
                    nextRgb[cIdx] = {float(wr / wSum), float(wg / wSum), float(wb / wSum)};
                }
            }
        }
        bilatRgb.swap(nextRgb);
        for (std::size_t i = 0; i < bilatRgb.size(); ++i) {
            if (roiMask[i]) {
                bilatLab[i] = srgbToLab(
                    std::uint8_t(std::clamp(int(std::lround(bilatRgb[i][0])), 0, 255)),
                    std::uint8_t(std::clamp(int(std::lround(bilatRgb[i][1])), 0, 255)),
                    std::uint8_t(std::clamp(int(std::lround(bilatRgb[i][2])), 0, 255)));
            }
        }
    }

    const EtfField etf = computeLabEtf(roiLab, rw, rh, req.stop);
    if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");

    const auto profileId = req.snapshot.spec.colorSpec.profileId;
    const auto makeColor = [&](double rUnit, double gUnit, double bUnit, double alpha = 1.0) noexcept {
        ColorValue c;
        c.profileId = profileId;
        c.activeChannels = 3;
        c.channels = {std::clamp(rUnit, 0.0, 1.0), std::clamp(gUnit, 0.0, 1.0),
                      std::clamp(bUnit, 0.0, 1.0), 0.0};
        c.alpha = std::clamp(alpha, 0.0, 1.0);
        return c;
    };

    std::uint64_t timeUs = 100;
    std::uint64_t totalEvents = 0;
    std::uint64_t totalSamples = 0;

    // =========================================================================
    // STAGE 1: artist.flats — Palette Quantization + Error-Gated / Smooth Level-Set Spans
    // =========================================================================
    const int maxK = isSuperRes ? 32 : 44;
    const int numClusters = std::clamp(int(std::sqrt(double(activePixels) / 200.0)), 4, maxK);
    struct PaletteEntry {
        LabPixel lab{};
        std::array<double, 3> rgbUnit{};
    };
    std::vector<PaletteEntry> palette;
    palette.reserve(std::size_t(numClusters));

    // Deterministic farthest-first Lab initialization inside object mask.
    std::vector<std::size_t> activeIndices;
    activeIndices.reserve(activePixels);
    for (std::size_t i = 0; i < roiMask.size(); ++i) {
        if (roiMask[i]) activeIndices.push_back(i);
    }
    const std::size_t stridePick = std::max<std::size_t>(1, activeIndices.size() / 4096);
    {
        const std::size_t firstIdx = activeIndices[activeIndices.size() / 2];
        palette.push_back({bilatLab[firstIdx],
                           {bilatRgb[firstIdx][0] / 255.0, bilatRgb[firstIdx][1] / 255.0, bilatRgb[firstIdx][2] / 255.0}});
        while (int(palette.size()) < numClusters) {
            double bestDist = -1.0;
            std::size_t bestIdx = firstIdx;
            for (std::size_t s = 0; s < activeIndices.size(); s += stridePick) {
                const std::size_t idx = activeIndices[s];
                double minD = std::numeric_limits<double>::infinity();
                for (const auto& p : palette) minD = std::min(minD, labDist2(bilatLab[idx], p.lab));
                if (minD > bestDist) {
                    bestDist = minD;
                    bestIdx = idx;
                }
            }
            if (bestDist < 3.5) break;
            palette.push_back({bilatLab[bestIdx],
                               {bilatRgb[bestIdx][0] / 255.0, bilatRgb[bestIdx][1] / 255.0, bilatRgb[bestIdx][2] / 255.0}});
        }
    }
    const int K = int(palette.size());
    std::vector<int> labels(roiMask.size(), -1);
    for (int iter = 0; iter < 6; ++iter) {
        std::vector<std::array<double, 3>> sumRgb(std::size_t(K), {0.0, 0.0, 0.0});
        std::vector<std::size_t> counts(std::size_t(K), 0);
        for (const std::size_t idx : activeIndices) {
            int bestK = 0;
            double bestD = 0.55 * labDist2(bilatLab[idx], palette[0].lab) + 0.45 * labDist2(roiLab[idx], palette[0].lab);
            for (int k = 1; k < K; ++k) {
                const double d = 0.55 * labDist2(bilatLab[idx], palette[std::size_t(k)].lab) +
                                 0.45 * labDist2(roiLab[idx], palette[std::size_t(k)].lab);
                if (d < bestD) { bestD = d; bestK = k; }
            }
            labels[idx] = bestK;
            sumRgb[std::size_t(bestK)][0] += double(bilatRgb[idx][0]);
            sumRgb[std::size_t(bestK)][1] += double(bilatRgb[idx][1]);
            sumRgb[std::size_t(bestK)][2] += double(bilatRgb[idx][2]);
            ++counts[std::size_t(bestK)];
        }
        for (int k = 0; k < K; ++k) {
            if (counts[std::size_t(k)] == 0) continue;
            const double inv = 1.0 / double(counts[std::size_t(k)]);
            const double r = sumRgb[std::size_t(k)][0] * inv;
            const double g = sumRgb[std::size_t(k)][1] * inv;
            const double b = sumRgb[std::size_t(k)][2] * inv;
            palette[std::size_t(k)].rgbUnit = {r / 255.0, g / 255.0, b / 255.0};
            palette[std::size_t(k)].lab = srgbToLab(
                std::uint8_t(std::clamp(int(std::lround(r)), 0, 255)),
                std::uint8_t(std::clamp(int(std::lround(g)), 0, 255)),
                std::uint8_t(std::clamp(int(std::lround(b)), 0, 255)));
        }
    }
    // Edge-preserving majority filter (3x3) to remove 1px noise speckles without erasing crisp 2px features.
    {
        std::vector<int> clean = labels;
        std::array<int, 64> hist{};
        for (int ly = 0; ly < rh; ++ly) {
            for (int lx = 0; lx < rw; ++lx) {
                const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!roiMask[idx]) continue;
                hist.fill(0);
                int bestK = labels[idx], bestVotes = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = std::clamp(ly + dy, 0, rh - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(lx + dx, 0, rw - 1);
                        const int lb = labels[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)];
                        if (lb >= 0 && lb < 64) {
                            const int v = ++hist[std::size_t(lb)];
                            if (v > bestVotes) { bestVotes = v; bestK = lb; }
                        }
                    }
                }
                const int curK = std::max(0, labels[idx]);
                if (bestK != curK && bestVotes >= 5 &&
                    labDist2(roiLab[idx], palette[std::size_t(bestK)].lab) <=
                        labDist2(roiLab[idx], palette[std::size_t(curK)].lab) + 36.0) {
                    clean[idx] = bestK;
                }
            }
        }
        labels.swap(clean);
    }

    // Build source-space ROI flats canvas (for accurate residual feedback in Stages 2-5)
    // AND emit target-resolution 4x4 supersampled multi-alpha engine8 events!
    TileData flatsRoi;
    flatsRoi.width = std::uint32_t(rw);
    flatsRoi.height = std::uint32_t(rh);
    flatsRoi.rowStride = std::uint64_t(rw) * 4;
    flatsRoi.format = src.format;
    flatsRoi.profileId = src.profileId;
    flatsRoi.bytes.assign(std::size_t(flatsRoi.rowStride) * std::size_t(rh), 0);

    for (int ly = 0; ly < rh; ++ly) {
        for (int lx = 0; lx < rw; ++lx) {
            const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
            if (!roiMask[idx]) continue;
            const int k = std::max(0, labels[idx]);
            const std::size_t pAt = std::size_t(ly) * flatsRoi.rowStride + std::size_t(lx) * 4;
            flatsRoi.bytes[pAt + 0] = std::uint8_t(std::clamp(int(std::lround(palette[std::size_t(k)].rgbUnit[0] * 255.0)), 0, 255));
            flatsRoi.bytes[pAt + 1] = std::uint8_t(std::clamp(int(std::lround(palette[std::size_t(k)].rgbUnit[1] * 255.0)), 0, 255));
            flatsRoi.bytes[pAt + 2] = std::uint8_t(std::clamp(int(std::lround(palette[std::size_t(k)].rgbUnit[2] * 255.0)), 0, 255));
            flatsRoi.bytes[pAt + 3] = 255;
        }
    }

    const std::uint32_t flatCellSide = (targetSize.width > 1600 || targetSize.height > 1600) ? 512u : 2048u;
    EventBatch flatsBatch{req.snapshot.revision, req.objectId, req.layers.flats, {}};
    if (!isSuperRes && req.clipToObjectMask) {
        // Exact full-coverage opaque palette base spans + error-gated Linear-sRGB transition spans
        // (smoothly blends atmospheric gradients and LINE_AA edges while keeping step edges razor-sharp!).
        std::vector<std::vector<std::array<std::uint32_t, 3>>> opaqueSpans(static_cast<std::size_t>(K));
        for (int ly = 0; ly < rh; ++ly) {
            const std::uint32_t gy = std::uint32_t(ry0 + ly);
            int lx = 0;
            while (lx < rw) {
                const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!roiMask[idx]) { ++lx; continue; }
                const int k = std::max(0, labels[idx]);
                int endX = lx + 1;
                while (endX < rw) {
                    const std::size_t nIdx = std::size_t(ly) * std::size_t(rw) + std::size_t(endX);
                    if (!roiMask[nIdx] || labels[nIdx] != k) break;
                    ++endX;
                }
                opaqueSpans[std::size_t(k)].push_back({std::uint32_t(rx0 + lx), std::uint32_t(rx0 + endX), gy});
                lx = endX;
            }
        }
        for (int k = 0; k < K; ++k) {
            const auto& c = palette[std::size_t(k)].rgbUnit;
            appendBucketChunks(flatsBatch.events, req.objectId, req.layers.flats, req.snapshot.revision,
                               makeColor(c[0], c[1], c[2], 1.0), req.seed, 10, opaqueSpans[std::size_t(k)],
                               timeUs, flatCellSide);
        }

        constexpr std::array<double, 3> tierAlphas{0.18, 0.34, 0.50};
        std::vector<std::array<double, 3>> palLin(static_cast<std::size_t>(K));
        for (int k = 0; k < K; ++k) {
            for (std::size_t c = 0; c < 3; ++c) {
                const auto s8 = std::uint8_t(std::clamp(int(std::lround(palette[std::size_t(k)].rgbUnit[c] * 255.0)), 0, 255));
                palLin[std::size_t(k)][c] = double(srgb8ToLinear16(s8)) / 65535.0;
            }
        }
        std::vector<std::array<std::vector<std::array<std::uint32_t, 3>>, 3>> aaSpans(static_cast<std::size_t>(K));
        std::vector<int> rowWinK(std::size_t(rw), -1);
        std::vector<int> rowWinTier(std::size_t(rw), -1);
        for (int ly = 0; ly < rh; ++ly) {
            const std::uint32_t gy = std::uint32_t(ry0 + ly);
            std::fill(rowWinK.begin(), rowWinK.end(), -1);
            std::fill(rowWinTier.begin(), rowWinTier.end(), -1);
            for (int lx = 0; lx < rw; ++lx) {
                const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!roiMask[idx]) continue;
                const int baseK = std::max(0, labels[idx]);
                const bool nearOuterSil = (lx == 0 || ly == 0 || lx + 1 == rw || ly + 1 == rh ||
                                           !roiMask[idx - 1] || !roiMask[idx + 1] ||
                                           !roiMask[idx - std::size_t(rw)] || !roiMask[idx + std::size_t(rw)]);
                std::array<int, 49> nCands{};
                int nCount = 0;
                constexpr std::array<int, 7> kTaps{{-16, -7, -2, 0, 2, 7, 16}};
                for (const int dy : kTaps) {
                    if (nearOuterSil && std::abs(dy) > 2) continue;
                    const int yy = std::clamp(ly + dy, 0, rh - 1);
                    for (const int dx : kTaps) {
                        if (nearOuterSil && std::abs(dx) > 2) continue;
                        const int xx = std::clamp(lx + dx, 0, rw - 1);
                        const std::size_t nIdx = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                        if (!roiMask[nIdx]) continue;
                        const int nk = labels[nIdx];
                        if (nk >= 0 && nk != baseK) {
                            bool seen = false;
                            for (int ci = 0; ci < nCount; ++ci) if (nCands[std::size_t(ci)] == nk) { seen = true; break; }
                            if (!seen) nCands[std::size_t(nCount++)] = nk;
                        }
                    }
                }
                if (nCount == 0) continue;
                const std::size_t fAt = std::size_t(ly) * flatsRoi.rowStride + std::size_t(lx) * 4;
                const std::size_t sAt = std::size_t(ry0 + ly) * src.rowStride + std::size_t(rx0 + lx) * 4;
                double bestErr = -3.0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double d0 = double(flatsRoi.bytes[fAt + c]) - double(src.bytes[sAt + c]);
                    bestErr += d0 * d0;
                }
                if (bestErr <= 0.0) continue;
                int winK = -1, winTier = -1;
                for (int ci = 0; ci < nCount; ++ci) {
                    const int nk = nCands[std::size_t(ci)];
                    for (int t = 0; t < 3; ++t) {
                        const double a = tierAlphas[std::size_t(t)];
                        double err = 0.0;
                        for (std::size_t c = 0; c < 3; ++c) {
                            const double linMix = (1.0 - a) * palLin[std::size_t(baseK)][c] + a * palLin[std::size_t(nk)][c];
                            const auto s8 = linear16ToSrgb8(std::uint16_t(std::clamp(int(std::lround(linMix * 65535.0)), 0, 65535)));
                            const double dc = double(s8) - double(src.bytes[sAt + c]);
                            err += dc * dc;
                        }
                        if (err < bestErr) {
                            bestErr = err;
                            winK = nk;
                            winTier = t;
                        }
                    }
                }
                rowWinK[std::size_t(lx)] = winK;
                rowWinTier[std::size_t(lx)] = winTier;
            }
            for (int lx = 0; lx < rw;) {
                if (rowWinK[std::size_t(lx)] < 0) { ++lx; continue; }
                const int wk = rowWinK[std::size_t(lx)];
                const int wt = rowWinTier[std::size_t(lx)];
                int endX = lx + 1;
                while (endX < rw && rowWinK[std::size_t(endX)] == wk && rowWinTier[std::size_t(endX)] == wt) ++endX;
                aaSpans[std::size_t(wk)][std::size_t(wt)].push_back(
                    {std::uint32_t(rx0 + lx), std::uint32_t(rx0 + endX), gy});
                lx = endX;
            }
        }
        for (int k = 0; k < K; ++k) {
            const auto& c = palette[std::size_t(k)].rgbUnit;
            for (std::size_t t = 0; t < 3; ++t) {
                const std::size_t prevSize = flatsBatch.events.size();
                appendBucketChunks(flatsBatch.events, req.objectId, req.layers.flats, req.snapshot.revision,
                                   makeColor(c[0], c[1], c[2], tierAlphas[t]), req.seed, 11 + std::uint32_t(t),
                                   aaSpans[std::size_t(k)][t], timeUs, flatCellSide);
                // Mirror all chunks into flatsRoi so Stage 2-5 residuals see exact base.
                for (std::size_t ei = prevSize; ei < flatsBatch.events.size(); ++ei) {
                    auto ok = rasterizeStrokeIntoRoi(flatsRoi, rx0, ry0, roiMask, flatsBatch.events[ei], req.stop);
                    if (!ok) return ok.error();
                }
            }
        }
    } else {
        // Unmasked or 300 DPI super-resolution mode (e.g. 1024x1536 -> 3300x5100):
        // Smooth outer silhouette AND continuous Lab-affinity Catmull-Rom level-sets + 4x4 subpixel
        // supersampling at native targetSize resolution so both outer silhouettes and internal curved
        // features (irises, catchlights, cel shadows, hair sheen) have monotonic Smooth 300 DPI curves!
        std::vector<float> smoothSil(std::size_t(rw) * std::size_t(rh), 0.0f);
        for (std::size_t i = 0; i < roiMask.size(); ++i) smoothSil[i] = roiMask[i] ? 1.0f : 0.0f;
        const auto smoothFieldNPass = [&](std::vector<float>& field, int passes) {
            for (int pass = 0; pass < passes; ++pass) {
                std::vector<float> tmp = field;
                for (int ly = 1; ly + 1 < rh; ++ly) {
                    for (int lx = 1; lx + 1 < rw; ++lx) {
                        const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                        float sum = 4.0f * field[idx];
                        sum += 2.0f * (field[idx - 1] + field[idx + 1] +
                                       field[idx - std::size_t(rw)] + field[idx + std::size_t(rw)]);
                        sum += field[idx - std::size_t(rw) - 1] + field[idx - std::size_t(rw) + 1] +
                               field[idx + std::size_t(rw) - 1] + field[idx + std::size_t(rw) + 1];
                        tmp[idx] = sum / 16.0f;
                    }
                }
                field.swap(tmp);
            }
        };
        smoothFieldNPass(smoothSil, 4);

        // Build extended label grid (dilated 2px into background) so cluster level-sets don't fall off at outer edges.
        std::vector<int> extLabels = labels;
        for (int dil = 0; dil < 2; ++dil) {
            std::vector<int> nextL = extLabels;
            for (int ly = 0; ly < rh; ++ly) {
                for (int lx = 0; lx < rw; ++lx) {
                    const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                    if (extLabels[idx] >= 0) continue;
                    for (int dy = -1; dy <= 1 && nextL[idx] < 0; ++dy) {
                        const int yy = std::clamp(ly + dy, 0, rh - 1);
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int xx = std::clamp(lx + dx, 0, rw - 1);
                            const int nb = extLabels[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)];
                            if (nb >= 0) { nextL[idx] = nb; break; }
                        }
                    }
                }
            }
            extLabels.swap(nextL);
        }
        // Initialize clusterFields using continuous source Lab affinity at boundary pixels so subpixel
        // LINE_AA coverage along curved irises, pupils, and catchlights is preserved instead of stairstepped!
        std::vector<std::vector<float>> clusterFields(
            std::size_t(K), std::vector<float>(std::size_t(rw) * std::size_t(rh), 0.0f));
        for (int ly = 0; ly < rh; ++ly) {
            for (int lx = 0; lx < rw; ++lx) {
                const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                const int lb0 = extLabels[idx];
                if (lb0 < 0 || lb0 >= K) continue;
                if (!roiMask[idx]) {
                    clusterFields[std::size_t(lb0)][idx] = 1.0f;
                    continue;
                }
                std::array<int, 9> locCands{};
                int nLoc = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = std::clamp(ly + dy, 0, rh - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(lx + dx, 0, rw - 1);
                        const int nb = extLabels[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)];
                        if (nb >= 0 && nb < K) {
                            bool seen = false;
                            for (int ci = 0; ci < nLoc; ++ci) if (locCands[std::size_t(ci)] == nb) { seen = true; break; }
                            if (!seen) locCands[std::size_t(nLoc++)] = nb;
                        }
                    }
                }
                if (nLoc <= 1) {
                    clusterFields[std::size_t(lb0)][idx] = 1.0f;
                } else {
                    std::array<double, 9> wts{};
                    double wSum = 0.0;
                    double minD2 = std::numeric_limits<double>::infinity();
                    for (int ci = 0; ci < nLoc; ++ci) {
                        minD2 = std::min(minD2, labDist2(roiLab[idx], palette[std::size_t(locCands[std::size_t(ci)])].lab));
                    }
                    for (int ci = 0; ci < nLoc; ++ci) {
                        const int ck = locCands[std::size_t(ci)];
                        const double d2 = labDist2(roiLab[idx], palette[std::size_t(ck)].lab) - minD2;
                        wts[std::size_t(ci)] = std::exp(-d2 / 42.0);
                        wSum += wts[std::size_t(ci)];
                    }
                    for (int ci = 0; ci < nLoc; ++ci) {
                        clusterFields[std::size_t(locCands[std::size_t(ci)])][idx] = float(wts[std::size_t(ci)] / std::max(1e-8, wSum));
                    }
                }
            }
        }
        // Preserve 1px thin features (e.g. iris rims beside specular catchlights) while smoothing
        // raster staircases for 4x4 Catmull-Rom level-set supersampling.
        for (int k = 0; k < K; ++k) {
            auto& field = clusterFields[std::size_t(k)];
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> tmp = field;
                for (int ly = 1; ly + 1 < rh; ++ly) {
                    for (int lx = 1; lx + 1 < rw; ++lx) {
                        const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                        float sum = 8.0f * field[idx];
                        sum += 2.0f * (field[idx - 1] + field[idx + 1] +
                                       field[idx - std::size_t(rw)] + field[idx + std::size_t(rw)]);
                        sum += field[idx - std::size_t(rw) - 1] + field[idx - std::size_t(rw) + 1] +
                               field[idx + std::size_t(rw) - 1] + field[idx + std::size_t(rw) + 1];
                        tmp[idx] = sum / 20.0f;
                    }
                }
                field.swap(tmp);
            }
        }

        const int tx0 = std::max(0, int(std::floor(double(rx0) * scaleX)) - 2);
        const int ty0 = std::max(0, int(std::floor(double(ry0) * scaleY)) - 2);
        const int tx1 = std::min(int(targetSize.width), int(std::ceil(double(rx1) * scaleX)) + 2);
        const int ty1 = std::min(int(targetSize.height), int(std::ceil(double(ry1) * scaleY)) + 2);
        // 4 alpha tiers (0.25, 0.50, 0.75, 1.00) per palette cluster + monotonic (< 0.50) internal boundary AA spans
        std::vector<std::array<std::vector<std::array<std::uint32_t, 3>>, 4>> superSpans(static_cast<std::size_t>(K));
        std::vector<std::array<std::vector<std::array<std::uint32_t, 3>>, 3>> internalAaSpans(static_cast<std::size_t>(K));
        const std::size_t spanWidth = std::size_t(std::max(1, tx1 - tx0));
        std::vector<int> rowK(spanWidth, -1);
        std::vector<int> rowTier(spanWidth, -1);
        std::vector<int> rowSecK(spanWidth, -1);
        std::vector<int> rowSecTier(spanWidth, -1);

        for (int ty = ty0; ty < ty1; ++ty) {
            if ((ty & 63) == 0 && req.stop.stop_requested())
                return error(ErrorCode::Cancelled, "artist.cancelled");
            std::fill(rowK.begin(), rowK.end(), -1);
            std::fill(rowTier.begin(), rowTier.end(), -1);
            std::fill(rowSecK.begin(), rowSecK.end(), -1);
            std::fill(rowSecTier.begin(), rowSecTier.end(), -1);
            for (int tx = tx0; tx < tx1; ++tx) {
                const double centerU = (double(tx) + 0.5) / scaleX - double(rx0);
                const double centerV = (double(ty) + 0.5) / scaleY - double(ry0);
                if (centerU < -1.0 || centerV < -1.0 || centerU > double(rw) + 1.0 || centerV > double(rh) + 1.0)
                    continue;
                const int nearX = std::clamp(int(std::floor(centerU)), 0, rw - 1);
                const int nearY = std::clamp(int(std::floor(centerV)), 0, rh - 1);
                const float quickVal = smoothSil[std::size_t(nearY) * std::size_t(rw) + std::size_t(nearX)];
                int hits = 0;
                if (quickVal >= 0.99f) {
                    hits = 16;
                } else if (quickVal <= 0.01f) {
                    hits = 0;
                } else {
                    for (int sy = 0; sy < 4; ++sy) {
                        const double sv = (double(ty) + (double(sy) + 0.5) * 0.25) / scaleY - double(ry0);
                        for (int sx = 0; sx < 4; ++sx) {
                            const double su = (double(tx) + (double(sx) + 0.5) * 0.25) / scaleX - double(rx0);
                            if (sampleSmoothMask(smoothSil, rw, rh, su, sv) >= 0.5) ++hits;
                        }
                    }
                }
                if (hits == 0) continue;
                const int tier = hits <= 4 ? 0 : (hits <= 8 ? 1 : (hits <= 13 ? 2 : 3));

                // Collect active candidate clusters in 3x3 source neighborhood.
                std::array<int, 9> cands{};
                int nCands = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = std::clamp(nearY + dy, 0, rh - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(nearX + dx, 0, rw - 1);
                        const int lb = extLabels[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)];
                        if (lb >= 0) {
                            bool seen = false;
                            for (int ci = 0; ci < nCands; ++ci) if (cands[std::size_t(ci)] == lb) { seen = true; break; }
                            if (!seen) cands[std::size_t(nCands++)] = lb;
                        }
                    }
                }
                int bestCluster = nCands > 0 ? cands[0] : 0;
                int secondCluster = -1, secondTier = -1;
                if (nCands > 1) {
                    // Multi-cluster transition zone: evaluate smooth Catmull-Rom level-sets on 4x4 subpixels!
                    std::array<int, 64> subVotes{};
                    for (int sy = 0; sy < 4; ++sy) {
                        const double sv = (double(ty) + (double(sy) + 0.5) * 0.25) / scaleY - double(ry0);
                        for (int sx = 0; sx < 4; ++sx) {
                            const double su = (double(tx) + (double(sx) + 0.5) * 0.25) / scaleX - double(rx0);
                            int winK = cands[0];
                            double winVal = -1e9;
                            for (int ci = 0; ci < nCands; ++ci) {
                                const int ck = cands[std::size_t(ci)];
                                const double val = sampleSmoothMask(clusterFields[std::size_t(ck)], rw, rh, su, sv);
                                if (val > winVal) { winVal = val; winK = ck; }
                            }
                            ++subVotes[std::size_t(winK)];
                        }
                    }
                    int topVotes = -1, runnerVotes = 0;
                    for (int ci = 0; ci < nCands; ++ci) {
                        const int ck = cands[std::size_t(ci)];
                        const int v = subVotes[std::size_t(ck)];
                        if (v > topVotes) {
                            runnerVotes = topVotes;
                            secondCluster = bestCluster;
                            topVotes = v;
                            bestCluster = ck;
                        } else if (v > runnerVotes) {
                            runnerVotes = v;
                            secondCluster = ck;
                        }
                    }
                    if (tier == 3 && secondCluster >= 0 && secondCluster != bestCluster && runnerVotes >= 2) {
                        secondTier = runnerVotes <= 4 ? 0 : (runnerVotes <= 6 ? 1 : 2);
                    }
                }
                rowK[std::size_t(tx - tx0)] = bestCluster;
                rowTier[std::size_t(tx - tx0)] = tier;
                rowSecK[std::size_t(tx - tx0)] = secondCluster;
                rowSecTier[std::size_t(tx - tx0)] = secondTier;
            }
            const int spanLen = tx1 - tx0;
            for (int idx = 0; idx < spanLen;) {
                if (rowK[std::size_t(idx)] < 0) { ++idx; continue; }
                const int k = rowK[std::size_t(idx)];
                const int tier = rowTier[std::size_t(idx)];
                int endIdx = idx + 1;
                while (endIdx < spanLen && rowK[std::size_t(endIdx)] == k && rowTier[std::size_t(endIdx)] == tier) {
                    ++endIdx;
                }
                superSpans[std::size_t(k)][std::size_t(tier)].push_back(
                    {std::uint32_t(tx0 + idx), std::uint32_t(tx0 + endIdx), std::uint32_t(ty)});
                idx = endIdx;
            }
            for (int idx = 0; idx < spanLen;) {
                if (rowSecK[std::size_t(idx)] < 0 || rowSecTier[std::size_t(idx)] < 0) { ++idx; continue; }
                const int sk = rowSecK[std::size_t(idx)];
                const int st = rowSecTier[std::size_t(idx)];
                int endIdx = idx + 1;
                while (endIdx < spanLen && rowSecK[std::size_t(endIdx)] == sk && rowSecTier[std::size_t(endIdx)] == st) {
                    ++endIdx;
                }
                internalAaSpans[std::size_t(sk)][std::size_t(st)].push_back(
                    {std::uint32_t(tx0 + idx), std::uint32_t(tx0 + endIdx), std::uint32_t(ty)});
                idx = endIdx;
            }
        }
        constexpr std::array<double, 4> alphas{0.25, 0.50, 0.75, 1.00};
        for (int k = 0; k < K; ++k) {
            const auto& c = palette[std::size_t(k)].rgbUnit;
            for (std::size_t t = 0; t < 4; ++t) {
                appendBucketChunks(flatsBatch.events, req.objectId, req.layers.flats, req.snapshot.revision,
                                   makeColor(c[0], c[1], c[2], alphas[t]), req.seed, 15 + std::uint32_t(t),
                                   superSpans[std::size_t(k)][t], timeUs, flatCellSide);
            }
        }
        // Strictly < 0.50 secondary alphas so 50/50 cluster crossings stay 100% monotonic without brightness inversion!
        constexpr std::array<double, 3> secAlphas{0.15, 0.28, 0.42};
        for (int k = 0; k < K; ++k) {
            const auto& c = palette[std::size_t(k)].rgbUnit;
            for (std::size_t t = 0; t < 3; ++t) {
                appendBucketChunks(flatsBatch.events, req.objectId, req.layers.flats, req.snapshot.revision,
                                   makeColor(c[0], c[1], c[2], secAlphas[t]), req.seed, 19 + std::uint32_t(t),
                                   internalAaSpans[std::size_t(k)][t], timeUs, flatCellSide);
            }
        }
    }

    for (const auto& ev : flatsBatch.events) {
        totalSamples += ev.geometry.size();
        ++totalEvents;
    }

    // Helper to composite current stage layers into a live working ROI buffer.
    TileData compositeAfterFlats = flatsRoi;

    // Precompute interior boundary distance transform & local 3x3 perceptual Lab contrast map.
    std::vector<float> maskDist(roiMask.size(), 0.0f);
    for (int ly = 0; ly < rh; ++ly) {
        for (int lx = 0; lx < rw; ++lx) {
            const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
            if (!roiMask[i]) { maskDist[i] = 0.0f; continue; }
            float best = 64.0f;
            if (lx > 0) best = std::min(best, maskDist[i - 1] + 1.0f);
            else best = 1.0f;
            if (ly > 0) best = std::min(best, maskDist[i - std::size_t(rw)] + 1.0f);
            else best = 1.0f;
            maskDist[i] = best;
        }
    }
    for (int ly = rh - 1; ly >= 0; --ly) {
        for (int lx = rw - 1; lx >= 0; --lx) {
            const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
            if (!roiMask[i]) continue;
            float best = maskDist[i];
            if (lx + 1 < rw) best = std::min(best, maskDist[i + 1] + 1.0f);
            else best = std::min(best, 1.0f);
            if (ly + 1 < rh) best = std::min(best, maskDist[i + std::size_t(rw)] + 1.0f);
            else best = std::min(best, 1.0f);
            maskDist[i] = best;
        }
    }
    std::vector<float> localContrast(roiLab.size(), 0.0f);
    for (int ly = 0; ly < rh; ++ly) {
        for (int lx = 0; lx < rw; ++lx) {
            const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
            if (!roiMask[i]) continue;
            double maxD2 = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = std::clamp(ly + dy, 0, rh - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = std::clamp(lx + dx, 0, rw - 1);
                    const std::size_t ni = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                    if (roiMask[ni]) maxD2 = std::max(maxD2, labDist2(roiLab[i], roiLab[ni]));
                }
            }
            localContrast[i] = float(std::sqrt(maxD2));
        }
    }

    // Trace helper along smooth Lab-ETF in source ROI coordinates [rx0..rx1, ry0..ry1].
    struct TracePoint {
        double gx{};
        double gy{};
        double pressure{};
    };
    const auto insideSourceMask = [&](double gx, double gy, float minClearance = 0.0f) noexcept -> bool {
        if (gx < double(rx0) || gy < double(ry0) || gx >= double(rx1) || gy >= double(ry1)) return false;
        const int lx = int(gx) - rx0, ly = int(gy) - ry0;
        const std::size_t idx = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
        return roiMask[idx] != 0 && maskDist[idx] >= minClearance;
    };
    const auto traceEtfStreamline = [&](int seedLx, int seedLy, double stepPx, int halfSteps, double maxLabJump, float minClearance = 0.0f) {
        std::vector<TracePoint> arms[2];
        const double cx = double(rx0 + seedLx) + 0.5, cy = double(ry0 + seedLy) + 0.5;
        const LabPixel originLab = bilatLab[std::size_t(seedLy) * std::size_t(rw) + std::size_t(seedLx)];
        const auto [dirX, dirY] = sampleEtf(etf, double(seedLx) + 0.5, double(seedLy) + 0.5);
        for (int side = 0; side < 2; ++side) {
            double px = cx, py = cy;
            double prevDx = side == 0 ? -dirX : dirX;
            double prevDy = side == 0 ? -dirY : dirY;
            for (int k = 0; k < halfSteps; ++k) {
                auto [tx1, ty1] = sampleEtf(etf, px - double(rx0), py - double(ry0));
                if (tx1 * prevDx + ty1 * prevDy < 0.0) { tx1 = -tx1; ty1 = -ty1; }
                const double midX = px + 0.5 * stepPx * tx1;
                const double midY = py + 0.5 * stepPx * ty1;
                if (!insideSourceMask(midX, midY, minClearance)) break;
                auto [tx2, ty2] = sampleEtf(etf, midX - double(rx0), midY - double(ry0));
                if (tx2 * prevDx + ty2 * prevDy < 0.0) { tx2 = -tx2; ty2 = -ty2; }
                const double dx = 0.75 * tx2 + 0.25 * prevDx;
                const double dy = 0.75 * ty2 + 0.25 * prevDy;
                const double dlen = std::hypot(dx, dy);
                if (dlen < 1e-5) break;
                const double nx = px + stepPx * (dx / dlen);
                const double ny = py + stepPx * (dy / dlen);
                if (!insideSourceMask(nx, ny, minClearance)) break;
                const int nlx = int(nx) - rx0, nly = int(ny) - ry0;
                if (labDist2(originLab, bilatLab[std::size_t(nly) * std::size_t(rw) + std::size_t(nlx)]) > maxLabJump * maxLabJump)
                    break;
                arms[side].push_back({nx, ny, 1.0});
                px = nx; py = ny;
                prevDx = dx / dlen; prevDy = dy / dlen;
            }
        }
        std::vector<TracePoint> curve;
        curve.reserve(arms[0].size() + 1 + arms[1].size());
        for (auto it = arms[0].rbegin(); it != arms[0].rend(); ++it) curve.push_back(*it);
        curve.push_back({cx, cy, 1.0});
        for (const auto& pt : arms[1]) curve.push_back(pt);
        const std::size_t n = curve.size();
        if (n >= 2) {
            for (std::size_t i = 0; i < n; ++i) {
                const double u = double(i) / double(n - 1);
                const double taper = std::sin(u * 3.14159265358979323846);
                curve[i].pressure = std::clamp(0.18 + 0.82 * std::pow(std::max(0.0, taper), 0.65), 0.12, 1.0);
            }
        }
        return curve;
    };

    // =========================================================================
    // STAGE 2: artist.form_glaze — Multiply Translucent Shadow Glazes
    // =========================================================================
    EventBatch glazeBatch{req.snapshot.revision, req.objectId, req.layers.formGlaze, {}};
    TileData glazeLayerRoi = flatsRoi;
    std::fill(glazeLayerRoi.bytes.begin(), glazeLayerRoi.bytes.end(), std::uint8_t(0));
    if (!req.layers.formGlaze.empty()) {
        constexpr std::uint8_t GlazeLayerOpacity = 217; // 0.85 * 255
        struct GlazeSeed {
            double depth{};
            int lx{}, ly{};
        };
        const int cell = std::max(6, int(std::min(rw, rh) / 26));
        const double glazeWidth = std::clamp(double(std::min(rw, rh)) / 10.0, 8.0, 48.0);
        const float glazeClear = (!req.clipToObjectMask) ? float(std::min(6.0, glazeWidth * 0.35)) : 0.0f;
        std::vector<GlazeSeed> seeds;
        for (int ly = 0; ly < rh; ly += cell) {
            for (int lx = 0; lx < rw; lx += cell) {
                GlazeSeed best{9.0, lx, ly};
                for (int yy = ly; yy < std::min(rh, ly + cell); ++yy) {
                    for (int xx = lx; xx < std::min(rw, lx + cell); ++xx) {
                        const std::size_t idx = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                        if (!roiMask[idx] || maskDist[idx] < glazeClear) continue;
                        const std::size_t fAt = std::size_t(yy) * flatsRoi.rowStride + std::size_t(xx) * 4;
                        const double flatLum = 0.299 * flatsRoi.bytes[fAt] + 0.587 * flatsRoi.bytes[fAt + 1] + 0.114 * flatsRoi.bytes[fAt + 2];
                        const double tgtLum = 0.299 * bilatRgb[idx][0] + 0.587 * bilatRgb[idx][1] + 0.114 * bilatRgb[idx][2];
                        const double d = flatLum - tgtLum;
                        if (d > best.depth) best = {d, xx, yy};
                    }
                }
                if (best.depth > 9.0) seeds.push_back(best);
            }
        }
        std::stable_sort(seeds.begin(), seeds.end(), [](const auto& a, const auto& b) { return a.depth > b.depth; });
        const std::size_t glazeBudget = std::min<std::size_t>(seeds.size(),
            std::min<std::size_t>(isSuperRes ? 16 : 36, req.maxTotalEvents / 8));
        for (const auto& sd : seeds) {
            if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
            if (glazeBatch.events.size() >= glazeBudget || totalEvents >= req.maxTotalEvents) break;
            const std::size_t cIdx = std::size_t(sd.ly) * std::size_t(rw) + std::size_t(sd.lx);
            const std::size_t fAt = std::size_t(sd.ly) * compositeAfterFlats.rowStride + std::size_t(sd.lx) * 4;
            const double curLum = 0.299 * compositeAfterFlats.bytes[fAt] + 0.587 * compositeAfterFlats.bytes[fAt + 1] + 0.114 * compositeAfterFlats.bytes[fAt + 2];
            const double tgtLum = 0.299 * bilatRgb[cIdx][0] + 0.587 * bilatRgb[cIdx][1] + 0.114 * bilatRgb[cIdx][2];
            if (curLum - tgtLum < 7.5) continue;

            auto curve = traceEtfStreamline(sd.lx, sd.ly, std::max(1.8, glazeWidth * 0.30), 15, 24.0, glazeClear);
            if (curve.size() < 3 || totalSamples + curve.size() > req.maxTotalSamples) continue;

            // Compute Multiply transmittance color C_glaze = target / flat.
            std::array<double, 3> multColor{};
            for (std::size_t c = 0; c < 3; ++c) {
                const double baseC = std::max(16.0, double(flatsRoi.bytes[fAt + c]));
                const double ratio = std::clamp(double(bilatRgb[cIdx][c]) / baseC, 0.18, 0.97);
                multColor[c] = std::clamp(1.0 - (1.0 - ratio) / 0.52, 0.10, 0.96);
            }
            StrokeEvent ev;
            ev.id = makeArtistEventId(req.objectId, req.layers.formGlaze, req.seed, 20, glazeBatch.events.size());
            ev.objectId = req.objectId;
            ev.layerId = req.layers.formGlaze;
            ev.parentRevision = req.snapshot.revision;
            ev.engineVersion = 2;
            ev.toolId = "marker";
            ev.seed = req.seed;
            ev.color = makeColor(multColor[0], multColor[1], multColor[2], 1.0);
            ev.style.width = glazeWidth;
            ev.style.opacity = 0.52;
            ev.style.hardness = 0.18;
            ev.startUs = timeUs;
            for (const auto& pt : curve) {
                ev.geometry.push_back({pt.gx, pt.gy, pt.pressure, {}, {}, timeUs, 0, StrokeCapabilityLinearOptical});
                timeUs += 100;
            }
            ev.endUs = ev.geometry.back().timestampUs;
            timeUs += 50;

            // Check live Multiply improvement on ROI before accepting.
            TileData trialGlaze = glazeLayerRoi;
            auto rOk = rasterizeStrokeIntoRoi(trialGlaze, rx0, ry0, roiMask, ev, req.stop);
            if (!rOk) return rOk.error();

            auto bBox = strokeBounds(ev).value();
            const int bx0 = std::max(0, int(bBox.x) - rx0), by0 = std::max(0, int(bBox.y) - ry0);
            const int bx1 = std::min(rw, int(bBox.x + std::int64_t(bBox.width)) - rx0);
            const int by1 = std::min(rh, int(bBox.y + std::int64_t(bBox.height)) - ry0);
            double gain = 0.0;
            for (int yy = by0; yy < by1; ++yy) {
                for (int xx = bx0; xx < bx1; ++xx) {
                    const std::size_t li = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                    if (!roiMask[li]) continue;
                    const std::size_t off = std::size_t(yy) * flatsRoi.rowStride + std::size_t(xx) * 4;
                    std::uint8_t cand[4]{flatsRoi.bytes[off], flatsRoi.bytes[off + 1], flatsRoi.bytes[off + 2], flatsRoi.bytes[off + 3]};
                    compositeRgba8Pixel(cand, trialGlaze.bytes.data() + off, GlazeLayerOpacity, BlendMode::Multiply, false);
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double oldErr = double(compositeAfterFlats.bytes[off + c]) - double(bilatRgb[li][c]);
                        const double newErr = double(cand[c]) - double(bilatRgb[li][c]);
                        gain += oldErr * oldErr - newErr * newErr;
                    }
                }
            }
            if (gain < 140.0) continue;

            std::swap(glazeLayerRoi, trialGlaze);
            for (int yy = by0; yy < by1; ++yy) {
                for (int xx = bx0; xx < bx1; ++xx) {
                    const std::size_t li = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                    if (!roiMask[li]) continue;
                    const std::size_t off = std::size_t(yy) * flatsRoi.rowStride + std::size_t(xx) * 4;
                    std::uint8_t px[4]{flatsRoi.bytes[off], flatsRoi.bytes[off + 1], flatsRoi.bytes[off + 2], flatsRoi.bytes[off + 3]};
                    compositeRgba8Pixel(px, glazeLayerRoi.bytes.data() + off, GlazeLayerOpacity, BlendMode::Multiply, false);
                    std::copy_n(px, 4, compositeAfterFlats.bytes.data() + off);
                }
            }

            StrokeEvent outEv = isSuperRes ? scaleContinuousEvent(std::move(ev), scaleX, scaleY, targetSize, false, {}) : std::move(ev);
            if (outEv.geometry.size() >= 2) {
                totalSamples += outEv.geometry.size();
                ++totalEvents;
                glazeBatch.events.push_back(std::move(outEv));
            }
        }
    }

    // =========================================================================
    // STAGE 3: artist.structure — Macro Atmospheric Washes + Lab-ETF Brushwork
    // =========================================================================
    EventBatch structureBatch{req.snapshot.revision, req.objectId, req.layers.structure, {}};
    TileData liveComposite = compositeAfterFlats;
    if (!req.layers.structure.empty()) {
        struct BrushPass {
            double width;
            std::uint32_t engine;
            const char* tool;
            double hardness;
            double opacity;
            int halfSteps;
            double maxLabJump;
            double minGain;
            double budgetFrac;
            bool softWash;
        };
        const double macroW1 = std::clamp(double(std::min(rw, rh)) / 8.5, 16.0, 52.0);
        const double macroW2 = std::clamp(double(std::min(rw, rh)) / 17.0, 9.0, 24.0);
        const std::array<BrushPass, 6> passes{{
            {macroW1, 2, "marker", 0.16, 0.60, 10, 24.0, 340.0, 0.05, true},
            {macroW2, 2, "marker", 0.26, 0.70, 11, 19.0, 180.0, 0.14, true},
            {5.0, 2, "marker", 0.44, 0.85, 13, 15.0, 75.0, 0.26, false},
            {2.8, 19, "smooth-ink-aa", 1.0, 0.92, 11, 13.0, 26.0, 0.48, false},
            {1.60, 19, "smooth-ink-aa", 1.0, 0.95, 8, 11.5, 9.0, 0.76, false},
            {1.00, 19, "smooth-ink-aa", 1.0, 0.96, 5, 10.0, 4.5, 1.00, false}
        }};
        const std::size_t targetStruct = isSuperRes ? 380 : std::clamp<std::size_t>(activePixels / 50, 600, 2400);
        const std::size_t structCap = std::min<std::size_t>(targetStruct, (req.maxTotalEvents - totalEvents) * 4 / 5);
        for (const auto& bp : passes) {
            // In super-resolution mode, skip 1px micro stippling so 300 DPI edges stay crisp vector curves.
            if (isSuperRes && bp.width < 1.5) continue;
            const std::size_t passLimit = std::size_t(double(structCap) * bp.budgetFrac);
            const int cell = std::max(2, int(std::ceil(bp.width * 1.15)));
            const float reqClearance = (!req.clipToObjectMask)
                                           ? float(std::min(6.0, std::max(1.0, bp.width * 0.48)))
                                           : 0.0f;
            const double minSqSeed = bp.width <= 1.8 ? 8.0 : (bp.softWash ? 28.0 : 16.0);
            const int maxSubPasses = (!isSuperRes && bp.width <= 2.0) ? 4 : ((!isSuperRes && bp.width <= 3.0) ? 3 : 2);
            for (int subPass = 0; subPass < maxSubPasses; ++subPass) {
                struct ErrSeed {
                    double err{};
                    int lx{}, ly{};
                };
                std::vector<ErrSeed> seeds;
                for (int ly = 0; ly < rh; ly += cell) {
                    for (int lx = 0; lx < rw; lx += cell) {
                        ErrSeed best{minSqSeed, lx, ly};
                        for (int yy = ly; yy < std::min(rh, ly + cell); ++yy) {
                            for (int xx = lx; xx < std::min(rw, lx + cell); ++xx) {
                                const std::size_t li = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                                if (!roiMask[li] || maskDist[li] < reqClearance) continue;
                                // Avoid seeding medium/wide strokes on 1px steep ink/cel edges in superRes mode
                                if (isSuperRes && bp.width >= 1.6 && localContrast[li] > 22.0f) continue;
                                // Soft washes target smooth low-contrast gradients, not crisp ink lines
                                if (bp.softWash && localContrast[li] > 18.0f) continue;
                                const std::size_t cAt = std::size_t(yy) * liveComposite.rowStride + std::size_t(xx) * 4;
                                const std::size_t sAt = std::size_t(ry0 + yy) * src.rowStride + std::size_t(rx0 + xx) * 4;
                                double sq = 0.0;
                                for (std::size_t c = 0; c < 3; ++c) {
                                    const double tgtVal = bp.softWash ? double(bilatRgb[li][c]) : double(src.bytes[sAt + c]);
                                    const double d = double(liveComposite.bytes[cAt + c]) - tgtVal;
                                    sq += d * d;
                                }
                                const double weight = bp.softWash
                                                          ? 1.0
                                                          : (1.0 + 2.0 * double(etf.mag[li]) + 0.12 * double(std::min(24.0f, localContrast[li])));
                                if (sq * weight > best.err) best = {sq * weight, xx, yy};
                            }
                        }
                        if (best.err > minSqSeed) seeds.push_back(best);
                    }
                }
                std::stable_sort(seeds.begin(), seeds.end(), [](const auto& a, const auto& b) { return a.err > b.err; });

                for (const auto& sd : seeds) {
                    if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
                    if (structureBatch.events.size() >= passLimit || totalEvents >= req.maxTotalEvents) break;
                    const std::size_t seedLi = std::size_t(sd.ly) * std::size_t(rw) + std::size_t(sd.lx);
                    const std::size_t cAt0 = std::size_t(sd.ly) * liveComposite.rowStride + std::size_t(sd.lx) * 4;
                    const std::size_t sAt0 = std::size_t(ry0 + sd.ly) * src.rowStride + std::size_t(rx0 + sd.lx) * 4;
                    double curSq = 0.0;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double tgtVal = bp.softWash ? double(bilatRgb[seedLi][c]) : double(src.bytes[sAt0 + c]);
                        const double d = double(liveComposite.bytes[cAt0 + c]) - tgtVal;
                        curSq += d * d;
                    }
                    if (curSq < minSqSeed * 0.8) continue;

                    const double stepLen = std::max(0.65, bp.width * (bp.softWash ? 0.35 : 0.45));
                    auto curve = traceEtfStreamline(sd.lx, sd.ly, stepLen, bp.halfSteps, bp.maxLabJump, reqClearance);
                    if (!isSuperRes && bp.width <= 2.0 && curve.size() == 1) {
                        const auto [dx0, dy0] = sampleEtf(etf, double(sd.lx) + 0.5, double(sd.ly) + 0.5);
                        const double cx = double(rx0 + sd.lx) + 0.5, cy = double(ry0 + sd.ly) + 0.5;
                        const double ax = cx - 0.40 * dx0, ay = cy - 0.40 * dy0;
                        const double bx = cx + 0.40 * dx0, by = cy + 0.40 * dy0;
                        if (insideSourceMask(ax, ay, reqClearance) && insideSourceMask(bx, by, reqClearance)) {
                            curve = {{ax, ay, 0.78}, {bx, by, 0.78}};
                        }
                    }
                    const std::size_t minCurvePts = (!isSuperRes && bp.width <= 2.0) ? 2 : 3;
                    if (curve.size() < minCurvePts || totalSamples + curve.size() > req.maxTotalSamples) continue;
                    double arcLen = 0.0;
                    for (std::size_t k = 1; k < curve.size(); ++k) {
                        arcLen += std::hypot(curve[k].gx - curve[k - 1].gx, curve[k].gy - curve[k - 1].gy);
                    }
                    // Prevent short stubby dot pimples along high-contrast edges
                    if (isSuperRes && arcLen < std::max(2.6, bp.width * 1.15)) continue;
                    if (!isSuperRes && bp.width >= 2.5 && arcLen < bp.width * 0.75) continue;

                    // Route textured mid-width fur/wood strokes to upgraded physical DryInk (engine12)!
                    const bool useTexDry = (!bp.softWash && !isSuperRes && bp.width >= 2.5 && bp.width <= 4.0 &&
                                            localContrast[seedLi] > 9.5f && (structureBatch.events.size() % 4 == 0));
                    const std::uint32_t evCap = StrokeCapabilityLinearOptical |
                                                (useTexDry ? StrokeCapabilityPhysicalDryInk : 0u);

                    StrokeEvent ev;
                    ev.id = makeArtistEventId(req.objectId, req.layers.structure, req.seed, 30, structureBatch.events.size());
                    ev.objectId = req.objectId;
                    ev.layerId = req.layers.structure;
                    ev.parentRevision = req.snapshot.revision;
                    ev.engineVersion = useTexDry ? 12 : bp.engine;
                    ev.toolId = useTexDry ? "dry-ink" : bp.tool;
                    ev.seed = useTexDry ? (req.seed ^ (0x517cc1b727220a95ull * (structureBatch.events.size() + 1))) : req.seed;
                    ev.color = makeColor(0.5, 0.5, 0.5, 1.0);
                    ev.style.width = bp.width;
                    ev.style.opacity = bp.opacity;
                    ev.style.hardness = useTexDry ? 1.0 : bp.hardness;
                    ev.startUs = timeUs;
                    for (const auto& pt : curve) {
                        ev.geometry.push_back({pt.gx, pt.gy, pt.pressure, {}, {}, timeUs, 0, evCap});
                        timeUs += 100;
                    }
                    ev.endUs = ev.geometry.back().timestampUs;
                    timeUs += 50;

                    auto bBox = strokeBounds(ev);
                    if (!bBox) continue;
                    const int bx0 = std::max(rx0, int(bBox.value().x));
                    const int by0 = std::max(ry0, int(bBox.value().y));
                    const int bx1 = std::min(rx1, int(bBox.value().x + std::int64_t(bBox.value().width)));
                    const int by1 = std::min(ry1, int(bBox.value().y + std::int64_t(bBox.value().height)));
                    if (bx0 >= bx1 || by0 >= by1) continue;

                    const unsigned boxW = unsigned(bx1 - bx0);
                    const unsigned boxH = unsigned(by1 - by0);
                    std::vector<std::uint16_t> fullCov(std::size_t(boxW) * std::size_t(boxH), 0);
                    for (int ty = by0; ty < by1; ty += int(TileSide)) {
                        for (int tx = bx0; tx < bx1; tx += int(TileSide)) {
                            const int tw = std::min(int(TileSide), bx1 - tx);
                            const int th = std::min(int(TileSide), by1 - ty);
                            Rect subBox{tx, ty, std::uint64_t(tw), std::uint64_t(th)};
                            auto subCov = strokeCoverage(ev, subBox, UINT64_MAX, req.stop);
                            if (!subCov) return subCov.error();
                            for (int py = 0; py < th; ++py) {
                                std::copy_n(subCov.value().data() + std::size_t(py) * std::size_t(tw),
                                            std::size_t(tw),
                                            fullCov.data() + std::size_t(ty + py - by0) * std::size_t(boxW) + std::size_t(tx - bx0));
                            }
                        }
                    }

                    TileData patch;
                    patch.width = boxW;
                    patch.height = boxH;
                    patch.rowStride = std::uint64_t(boxW) * 4;
                    patch.format = src.format;
                    patch.profileId = src.profileId;
                    patch.bytes.resize(std::size_t(patch.rowStride) * std::size_t(boxH));

                    // Solve optimal least-squares linear-sRGB stroke color over covered footprint!
                    std::array<double, 3> numLin{0.0, 0.0, 0.0};
                    double denLin = 0.0;
                    for (unsigned py = 0; py < patch.height; ++py) {
                        const std::size_t cOff = std::size_t(int(py) + by0 - ry0) * liveComposite.rowStride + std::size_t(bx0 - rx0) * 4;
                        std::copy_n(liveComposite.bytes.data() + cOff, patch.rowStride, patch.bytes.data() + std::size_t(py) * patch.rowStride);
                        for (unsigned px = 0; px < patch.width; ++px) {
                            auto& cv = fullCov[std::size_t(py) * patch.width + px];
                            const std::size_t mLi = std::size_t(int(py) + by0 - ry0) * std::size_t(rw) + std::size_t(int(px) + bx0 - rx0);
                            if (!roiMask[mLi]) {
                                cv = 0;
                                continue;
                            }
                            const double a = (double(cv) / 65535.0) * bp.opacity;
                            if (a <= 1e-4) continue;
                            denLin += a * a;
                            const std::size_t pC = std::size_t(int(py) + by0 - ry0) * liveComposite.rowStride + std::size_t(int(px) + bx0 - rx0) * 4;
                            const std::size_t pS = std::size_t(by0 + int(py)) * src.rowStride + std::size_t(bx0 + int(px)) * 4;
                            for (std::size_t c = 0; c < 3; ++c) {
                                const double baseLin = double(srgb8ToLinear16(liveComposite.bytes[pC + c])) / 65535.0;
                                const std::uint8_t tgtByte = bp.softWash
                                    ? std::uint8_t(std::clamp(int(std::lround(bilatRgb[mLi][c])), 0, 255))
                                    : src.bytes[pS + c];
                                const double tgtLin = double(srgb8ToLinear16(tgtByte)) / 65535.0;
                                numLin[c] += a * (tgtLin - (1.0 - a) * baseLin);
                            }
                        }
                    }
                    if (denLin < 1e-6) continue;
                    std::array<double, 3> solvedSrgb{};
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double optLin = std::clamp(numLin[c] / denLin, 0.0, 1.0);
                        const auto lin16 = std::uint16_t(std::lround(optLin * 65535.0));
                        solvedSrgb[c] = double(linear16ToSrgb8(lin16)) / 255.0;
                    }
                    ev.color = makeColor(solvedSrgb[0], solvedSrgb[1], solvedSrgb[2], 1.0);

                    blendCoverage(patch, ev, fullCov);
                    double gain = 0.0;
                    for (unsigned py = 0; py < patch.height; ++py) {
                        for (unsigned px = 0; px < patch.width; ++px) {
                            const int lx = bx0 - rx0 + int(px), ly = by0 - ry0 + int(py);
                            const std::size_t mLi = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                            if (!roiMask[mLi]) continue;
                            const std::size_t cOff = std::size_t(ly) * liveComposite.rowStride + std::size_t(lx) * 4;
                            const std::size_t sOff = std::size_t(by0 + int(py)) * src.rowStride + std::size_t(bx0 + int(px)) * 4;
                            const std::size_t pOff = std::size_t(py) * patch.rowStride + std::size_t(px) * 4;
                            for (std::size_t c = 0; c < 3; ++c) {
                                const double tgtVal = bp.softWash ? double(bilatRgb[mLi][c]) : double(src.bytes[sOff + c]);
                                const double dOld = double(liveComposite.bytes[cOff + c]) - tgtVal;
                                const double dNew = double(patch.bytes[pOff + c]) - tgtVal;
                                gain += dOld * dOld - dNew * dNew;
                            }
                        }
                    }
                    if (gain < bp.minGain) continue;
                    for (unsigned py = 0; py < patch.height; ++py) {
                        const std::size_t cOff = std::size_t(int(py) + by0 - ry0) * liveComposite.rowStride + std::size_t(bx0 - rx0) * 4;
                        std::copy_n(patch.bytes.data() + std::size_t(py) * patch.rowStride, patch.rowStride,
                                    liveComposite.bytes.data() + cOff);
                    }
                    StrokeEvent outEv = isSuperRes ? scaleContinuousEvent(std::move(ev), scaleX, scaleY, targetSize, false, {}) : std::move(ev);
                    if (outEv.geometry.size() >= 2) {
                        totalSamples += outEv.geometry.size();
                        ++totalEvents;
                        structureBatch.events.push_back(std::move(outEv));
                    }
                }
            }
        }
    }

    // =========================================================================
    // STAGE 4: artist.hero_ink — Steger/XDoG Ridge Topology & Multi-Brush Routing
    //   Routes to engine19 (smooth-ink-aa), engine12 (physical dry-ink), and engine21 (chisel-ink-aa)
    // =========================================================================
    EventBatch inkBatch{req.snapshot.revision, req.objectId, req.layers.heroInk, {}};
    if (!req.layers.heroInk.empty()) {
        // Compute local DoG / Hessian dark ridge response on L* channel.
        std::vector<float> lumBlurSmall(roiLab.size(), 0.0f);
        std::vector<float> lumBlurWide(roiLab.size(), 0.0f);
        const auto getL = [&](int lx, int ly) noexcept -> float {
            return roiLab[std::size_t(std::clamp(ly, 0, rh - 1)) * std::size_t(rw) + std::size_t(std::clamp(lx, 0, rw - 1))].L;
        };
        for (int ly = 0; ly < rh; ++ly) {
            for (int lx = 0; lx < rw; ++lx) {
                float sSmall = 0.0f, wSmall = 0.0f, sWide = 0.0f, wWide = 0.0f;
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        const float v = getL(lx + dx, ly + dy);
                        const float r2 = float(dx * dx + dy * dy);
                        if (std::abs(dx) <= 1 && std::abs(dy) <= 1) {
                            const float ws = std::exp(-r2 / 1.2f);
                            sSmall += ws * v; wSmall += ws;
                        }
                        const float ww = std::exp(-r2 / 7.5f);
                        sWide += ww * v; wWide += ww;
                    }
                }
                const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                lumBlurSmall[i] = sSmall / wSmall;
                lumBlurWide[i] = sWide / wWide;
            }
        }
        // Dark ridge map + non-maximum suppression perpendicular to ETF tangent.
        std::vector<float> ridgeStrength(roiLab.size(), 0.0f);
        std::vector<std::uint8_t> ridgeBinary(roiLab.size(), 0);
        for (int ly = 1; ly + 1 < rh; ++ly) {
            for (int lx = 1; lx + 1 < rw; ++lx) {
                const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!roiMask[i]) continue;
                const float xdog = lumBlurWide[i] - lumBlurSmall[i]; // positive on dark ink lines / dark creases
                if (xdog > 4.2f) {
                    ridgeStrength[i] = xdog;
                    ridgeBinary[i] = 1;
                }
            }
        }
        // Estimate local half-width via radial scan perpendicular to ETF tangent.
        std::vector<float> localHalfWidth(roiLab.size(), 0.8f);
        std::vector<std::uint8_t> ridgePeaks(roiLab.size(), 0);
        for (int ly = 1; ly + 1 < rh; ++ly) {
            for (int lx = 1; lx + 1 < rw; ++lx) {
                const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (!ridgeBinary[i]) continue;
                const double nx = -double(etf.ty[i]), ny = double(etf.tx[i]);
                const int px1 = std::clamp(int(std::lround(double(lx) + nx)), 0, rw - 1);
                const int py1 = std::clamp(int(std::lround(double(ly) + ny)), 0, rh - 1);
                const int px2 = std::clamp(int(std::lround(double(lx) - nx)), 0, rw - 1);
                const int py2 = std::clamp(int(std::lround(double(ly) - ny)), 0, rh - 1);
                const float s0 = ridgeStrength[i];
                const float s1 = ridgeStrength[std::size_t(py1) * std::size_t(rw) + std::size_t(px1)];
                const float s2 = ridgeStrength[std::size_t(py2) * std::size_t(rw) + std::size_t(px2)];
                if (s0 >= s1 && s0 >= s2) {
                    ridgePeaks[i] = 1;
                    float hw = 0.6f;
                    for (int side : {-1, 1}) {
                        for (int step = 1; step <= 6; ++step) {
                            const int sx = std::clamp(int(std::lround(double(lx) + side * step * nx)), 0, rw - 1);
                            const int sy = std::clamp(int(std::lround(double(ly) + side * step * ny)), 0, rh - 1);
                            if (ridgeBinary[std::size_t(sy) * std::size_t(rw) + std::size_t(sx)]) {
                                hw = std::max(hw, float(step) * 0.65f);
                            } else {
                                break;
                            }
                        }
                    }
                    localHalfWidth[i] = hw;
                }
            }
        }

        // Trace connected ridge polylines with junction angle-continuity linking.
        std::vector<std::uint8_t> visited(roiLab.size(), 0);
        struct RidgeSeed {
            float strength{};
            int lx{}, ly{};
        };
        const float inkClearance = (!req.clipToObjectMask) ? 1.5f : 0.0f;
        std::vector<RidgeSeed> rSeeds;
        for (int ly = 1; ly + 1 < rh; ++ly) {
            for (int lx = 1; lx + 1 < rw; ++lx) {
                const std::size_t i = std::size_t(ly) * std::size_t(rw) + std::size_t(lx);
                if (ridgePeaks[i] && maskDist[i] >= inkClearance) rSeeds.push_back({ridgeStrength[i], lx, ly});
            }
        }
        std::stable_sort(rSeeds.begin(), rSeeds.end(), [](const auto& a, const auto& b) { return a.strength > b.strength; });

        const std::size_t inkCap = std::min<std::size_t>(900, (req.maxTotalEvents - totalEvents) * 3 / 4);
        for (const auto& rs : rSeeds) {
            if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
            if (inkBatch.events.size() >= inkCap || totalEvents >= req.maxTotalEvents) break;
            const std::size_t startIdx = std::size_t(rs.ly) * std::size_t(rw) + std::size_t(rs.lx);
            if (visited[startIdx]) continue;

            struct InkNode {
                double gx{}, gy{}, halfW{};
                float texVar{};
            };
            std::vector<InkNode> arms[2];
            const auto [t0x, t0y] = sampleEtf(etf, double(rs.lx) + 0.5, double(rs.ly) + 0.5);
            visited[startIdx] = 1;

            for (int side = 0; side < 2; ++side) {
                int cx = rs.lx, cy = rs.ly;
                double dirX = side == 0 ? -t0x : t0x;
                double dirY = side == 0 ? -t0y : t0y;
                for (int step = 0; step < 96; ++step) {
                    int bestX = -1, bestY = -1;
                    double bestScore = 0.45;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const int nx = cx + dx, ny = cy + dy;
                            if (nx <= 0 || ny <= 0 || nx + 1 >= rw || ny + 1 >= rh) continue;
                            const std::size_t ni = std::size_t(ny) * std::size_t(rw) + std::size_t(nx);
                            if (visited[ni] || !roiMask[ni] || !ridgePeaks[ni]) continue;
                            const double dlen = std::hypot(double(dx), double(dy));
                            const double cosAng = (double(dx) * dirX + double(dy) * dirY) / dlen;
                            if (cosAng < 0.55) continue; // Junction angle-continuity gate
                            const double score = cosAng * (0.6 + 0.4 * std::min(1.0, double(ridgeStrength[ni]) / 20.0)) / (0.8 + 0.2 * dlen);
                            if (score > bestScore) {
                                bestScore = score;
                                bestX = nx;
                                bestY = ny;
                            }
                        }
                    }
                    if (bestX < 0) break;
                    const std::size_t bi = std::size_t(bestY) * std::size_t(rw) + std::size_t(bestX);
                    visited[bi] = 1;
                    // Mark 1px perpendicular neighborhood visited to prevent parallel duplicate traces.
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int vx = std::clamp(bestX + ox, 0, rw - 1);
                            const int vy = std::clamp(bestY + oy, 0, rh - 1);
                            visited[std::size_t(vy) * std::size_t(rw) + std::size_t(vx)] = 1;
                        }
                    }
                    const double stepDx = double(bestX - cx), stepDy = double(bestY - cy);
                    const double sl = std::hypot(stepDx, stepDy);
                    dirX = 0.65 * (stepDx / sl) + 0.35 * dirX;
                    dirY = 0.65 * (stepDy / sl) + 0.35 * dirY;
                    const double dl = std::hypot(dirX, dirY);
                    dirX /= dl; dirY /= dl;

                    const float lDiff = std::abs(roiLab[bi].L - bilatLab[bi].L);
                    arms[side].push_back({double(rx0 + bestX) + 0.5, double(ry0 + bestY) + 0.5,
                                          double(localHalfWidth[bi]), lDiff});
                    cx = bestX; cy = bestY;
                }
            }

            std::vector<InkNode> chain;
            chain.reserve(arms[0].size() + 1 + arms[1].size());
            for (auto it = arms[0].rbegin(); it != arms[0].rend(); ++it) chain.push_back(*it);
            chain.push_back({double(rx0 + rs.lx) + 0.5, double(ry0 + rs.ly) + 0.5,
                             double(localHalfWidth[startIdx]), std::abs(roiLab[startIdx].L - bilatLab[startIdx].L)});
            for (const auto& nd : arms[1]) chain.push_back(nd);
            if (chain.size() < 4 || totalSamples + chain.size() > req.maxTotalSamples) continue;

            // Multi-pass curve smoothing while keeping every sample strictly inside sourceMask.
            for (int smPass = 0; smPass < 3; ++smPass) {
                std::vector<InkNode> sm = chain;
                for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
                    const double sx = 0.25 * chain[i - 1].gx + 0.50 * chain[i].gx + 0.25 * chain[i + 1].gx;
                    const double sy = 0.25 * chain[i - 1].gy + 0.50 * chain[i].gy + 0.25 * chain[i + 1].gy;
                    if (insideSourceMask(sx, sy)) {
                        sm[i].gx = sx;
                        sm[i].gy = sy;
                    }
                }
                chain.swap(sm);
            }

            double maxDiam = 0.8, meanTex = 0.0, maxTurn = 0.0;
            double lumMean = 0.0, lumM2 = 0.0;
            std::array<double, 3> inkRgb{0.0, 0.0, 0.0};
            for (std::size_t i = 0; i < chain.size(); ++i) {
                maxDiam = std::max(maxDiam, chain[i].halfW * 2.0);
                meanTex += double(chain[i].texVar);
                const int px = std::clamp(int(chain[i].gx), 0, sw - 1);
                const int py = std::clamp(int(chain[i].gy), 0, sh - 1);
                const std::size_t sAt = std::size_t(py) * src.rowStride + std::size_t(px) * 4;
                const double rU = double(src.bytes[sAt + 0]) / 255.0;
                const double gU = double(src.bytes[sAt + 1]) / 255.0;
                const double bU = double(src.bytes[sAt + 2]) / 255.0;
                inkRgb[0] += rU;
                inkRgb[1] += gU;
                inkRgb[2] += bU;
                const double lum = (0.299 * rU + 0.587 * gU + 0.114 * bU) * 255.0;
                const double deltaL = lum - lumMean;
                lumMean += deltaL / double(i + 1);
                lumM2 += deltaL * (lum - lumMean);
                if (i >= 1 && i + 1 < chain.size()) {
                    const double ax = chain[i].gx - chain[i - 1].gx, ay = chain[i].gy - chain[i - 1].gy;
                    const double bx = chain[i + 1].gx - chain[i].gx, by = chain[i + 1].gy - chain[i].gy;
                    const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
                    if (la > 1e-4 && lb > 1e-4) {
                        const double d = std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0);
                        maxTurn = std::max(maxTurn, std::acos(d));
                    }
                }
            }
            meanTex /= double(chain.size());
            const double ridgeLumStd = std::sqrt(lumM2 / double(std::max<std::size_t>(1, chain.size())));
            for (auto& ch : inkRgb) ch /= double(chain.size());

            // Multi-brush routing:
            // - Textured / rough natural strokes -> engine12 (upgraded physical dry-ink)
            // - Crisp angular calligraphic strokes -> engine21 (chisel-ink-aa, seed == 0)
            // - Clean solid anime / cel linework -> engine19 (smooth-ink-aa, zero dry-tooth pimples!)
            StrokeEvent ev;
            ev.id = makeArtistEventId(req.objectId, req.layers.heroInk, req.seed, 40, inkBatch.events.size());
            ev.objectId = req.objectId;
            ev.layerId = req.layers.heroInk;
            ev.parentRevision = req.snapshot.revision;
            ev.color = makeColor(inkRgb[0], inkRgb[1], inkRgb[2], 1.0);

            std::uint32_t caps = StrokeCapabilityLinearOptical;
            if (!isSuperRes && maxDiam >= 2.6 && meanTex > 4.2 && ridgeLumStd > 8.5) {
                ev.engineVersion = 12;
                ev.toolId = "dry-ink";
                ev.seed = req.seed ^ (0x9e3779b97f4a7c15ull * (inkBatch.events.size() + 1));
                ev.style.width = std::clamp(maxDiam * 1.12, 0.5, 48.0);
                ev.style.opacity = 0.92;
                ev.style.hardness = 1.0;
                ev.style.shape = NibShape::Round;
                ev.style.angle = 0;
                caps |= StrokeCapabilityPhysicalDryInk;
            } else if (!isSuperRes && maxTurn > 0.65 && maxDiam >= 1.5 && ridgeLumStd > 5.0) {
                ev.engineVersion = 21;
                ev.toolId = "chisel-ink-aa";
                ev.seed = 0; // Required by validateChiselInkEvent
                ev.style.width = std::clamp(maxDiam * 1.1, 0.6, 32.0);
                ev.style.opacity = 0.94;
                ev.style.hardness = 1.0;
                ev.style.shape = NibShape::Chisel;
                const double dx = chain.back().gx - chain.front().gx;
                const double dy = chain.back().gy - chain.front().gy;
                const double ang = std::fmod(std::atan2(dy, dx) + 3.14159265358979323846, 3.14159265358979323846);
                ev.style.angle = std::uint8_t(std::clamp(int(std::lround(ang / (3.14159265358979323846 / 8.0))) % 8, 0, 7));
            } else {
                ev.engineVersion = 19;
                ev.toolId = "smooth-ink-aa";
                ev.seed = req.seed;
                ev.style.width = std::clamp(maxDiam, 0.45, 24.0);
                ev.style.opacity = 0.95;
                ev.style.hardness = 1.0;
                ev.style.shape = NibShape::Round;
                ev.style.angle = 0;
            }

            ev.startUs = timeUs;
            for (std::size_t i = 0; i < chain.size(); ++i) {
                const double u = double(i) / double(chain.size() - 1);
                const double endTaper = std::min({1.0, u / 0.16, (1.0 - u) / 0.16});
                const double widthRatio = std::clamp((chain[i].halfW * 2.0) / std::max(0.4, ev.style.width), 0.25, 1.0);
                const double p = std::clamp((0.20 + 0.80 * endTaper) * widthRatio, 0.10, 1.0);
                ev.geometry.push_back({chain[i].gx, chain[i].gy, p, {}, {}, timeUs, 0, caps});
                timeUs += 100;
            }
            ev.endUs = ev.geometry.back().timestampUs;
            timeUs += 50;

            // Verify positive visual error reduction on liveComposite before committing.
            TileData trial = liveComposite;
            auto rOk = rasterizeStrokeIntoRoi(trial, rx0, ry0, roiMask, ev, req.stop);
            if (!rOk) continue;
            auto bBox = strokeBounds(ev).value();
            const int bx0 = std::max(0, int(bBox.x) - rx0), by0 = std::max(0, int(bBox.y) - ry0);
            const int bx1 = std::min(rw, int(bBox.x + std::int64_t(bBox.width)) - rx0);
            const int by1 = std::min(rh, int(bBox.y + std::int64_t(bBox.height)) - ry0);
            double gain = 0.0;
            for (int yy = by0; yy < by1; ++yy) {
                for (int xx = bx0; xx < bx1; ++xx) {
                    if (!roiMask[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)]) continue;
                    const std::size_t cOff = std::size_t(yy) * liveComposite.rowStride + std::size_t(xx) * 4;
                    const std::size_t sOff = std::size_t(ry0 + yy) * src.rowStride + std::size_t(rx0 + xx) * 4;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double dOld = double(liveComposite.bytes[cOff + c]) - double(src.bytes[sOff + c]);
                        const double dNew = double(trial.bytes[cOff + c]) - double(src.bytes[sOff + c]);
                        gain += dOld * dOld - dNew * dNew;
                    }
                }
            }
            if (gain < 32.0) continue;
            std::swap(liveComposite, trial);

            StrokeEvent outEv = isSuperRes ? scaleContinuousEvent(std::move(ev), scaleX, scaleY, targetSize, false, {}) : std::move(ev);
            if (outEv.geometry.size() >= 2) {
                totalSamples += outEv.geometry.size();
                ++totalEvents;
                inkBatch.events.push_back(std::move(outEv));
            }
        }
    }

    // =========================================================================
    // STAGE 5: artist.highlights — Screen Specular Catchlights & Rim Accents
    // =========================================================================
    EventBatch hiBatch{req.snapshot.revision, req.objectId, req.layers.highlights, {}};
    TileData hiLayerRoi = flatsRoi;
    std::fill(hiLayerRoi.bytes.begin(), hiLayerRoi.bytes.end(), std::uint8_t(0));
    if (!req.layers.highlights.empty()) {
        constexpr std::uint8_t HiLayerOpacity = 230; // 0.90 * 255
        struct HiSeed {
            double boost{};
            int lx{}, ly{};
        };
        const int cell = 6;
        const float hiClearance = (!req.clipToObjectMask) ? 2.0f : 0.0f;
        std::vector<HiSeed> hSeeds;
        for (int ly = 0; ly < rh; ly += cell) {
            for (int lx = 0; lx < rw; lx += cell) {
                HiSeed best{12.0, lx, ly};
                for (int yy = ly; yy < std::min(rh, ly + cell); ++yy) {
                    for (int xx = lx; xx < std::min(rw, lx + cell); ++xx) {
                        const std::size_t li = std::size_t(yy) * std::size_t(rw) + std::size_t(xx);
                        if (!roiMask[li] || maskDist[li] < hiClearance) continue;
                        if (isSuperRes && localContrast[li] > 20.0f) continue;
                        const std::size_t cOff = std::size_t(yy) * liveComposite.rowStride + std::size_t(xx) * 4;
                        const std::size_t sOff = std::size_t(ry0 + yy) * src.rowStride + std::size_t(rx0 + xx) * 4;
                        const double curLum = 0.299 * liveComposite.bytes[cOff] + 0.587 * liveComposite.bytes[cOff + 1] + 0.114 * liveComposite.bytes[cOff + 2];
                        const double tgtLum = 0.299 * src.bytes[sOff] + 0.587 * src.bytes[sOff + 1] + 0.114 * src.bytes[sOff + 2];
                        if (tgtLum - curLum > best.boost) best = {tgtLum - curLum, xx, yy};
                    }
                }
                if (best.boost > 12.0) hSeeds.push_back(best);
            }
        }
        std::stable_sort(hSeeds.begin(), hSeeds.end(), [](const auto& a, const auto& b) { return a.boost > b.boost; });
        const std::size_t hiCap = std::min<std::size_t>(240, req.maxTotalEvents - totalEvents);
        const TileData baseBeforeHi = liveComposite;

        for (const auto& hs : hSeeds) {
            if (req.stop.stop_requested()) return error(ErrorCode::Cancelled, "artist.cancelled");
            if (hiBatch.events.size() >= hiCap || totalEvents >= req.maxTotalEvents) break;
            const std::size_t cOff0 = std::size_t(hs.ly) * liveComposite.rowStride + std::size_t(hs.lx) * 4;
            const std::size_t sOff0 = std::size_t(ry0 + hs.ly) * src.rowStride + std::size_t(rx0 + hs.lx) * 4;
            const double curL = 0.299 * liveComposite.bytes[cOff0] + 0.587 * liveComposite.bytes[cOff0 + 1] + 0.114 * liveComposite.bytes[cOff0 + 2];
            const double tgtL = 0.299 * src.bytes[sOff0] + 0.587 * src.bytes[sOff0 + 1] + 0.114 * src.bytes[sOff0 + 2];
            if (tgtL - curL < 10.0) continue;

            auto curve = traceEtfStreamline(hs.lx, hs.ly, 1.2, 8, 16.0, hiClearance);
            if (!isSuperRes && curve.size() == 1) {
                const auto [dx0, dy0] = sampleEtf(etf, double(hs.lx) + 0.5, double(hs.ly) + 0.5);
                const double cx = double(rx0 + hs.lx) + 0.5, cy = double(ry0 + hs.ly) + 0.5;
                const double ax = cx - 0.45 * dx0, ay = cy - 0.45 * dy0;
                const double bx = cx + 0.45 * dx0, by = cy + 0.45 * dy0;
                if (insideSourceMask(ax, ay, hiClearance) && insideSourceMask(bx, by, hiClearance)) {
                    curve = {{ax, ay, 0.85}, {bx, by, 0.85}};
                }
            }
            const std::size_t minHiPts = isSuperRes ? 4 : 2;
            if (curve.size() < minHiPts || totalSamples + curve.size() > req.maxTotalSamples) continue;
            if (isSuperRes) {
                double hiArc = 0.0;
                for (std::size_t k = 1; k < curve.size(); ++k) {
                    hiArc += std::hypot(curve[k].gx - curve[k - 1].gx, curve[k].gy - curve[k - 1].gy);
                }
                if (hiArc < 4.5) continue;
            }

            // Solve Screen source color: screened = 1 - (1 - cs)*(1 - cb) => cs = (tgt - cb) / (1 - cb)
            std::array<double, 3> screenRgb{};
            for (std::size_t c = 0; c < 3; ++c) {
                const double cb = double(baseBeforeHi.bytes[cOff0 + c]) / 255.0;
                const double tg = double(src.bytes[sOff0 + c]) / 255.0;
                const double denom = std::max(0.08, (1.0 - cb) * 0.65);
                screenRgb[c] = std::clamp((tg - cb) / denom, 0.04, 1.0);
            }

            StrokeEvent ev;
            ev.id = makeArtistEventId(req.objectId, req.layers.highlights, req.seed, 50, hiBatch.events.size());
            ev.objectId = req.objectId;
            ev.layerId = req.layers.highlights;
            ev.parentRevision = req.snapshot.revision;
            ev.engineVersion = 19;
            ev.toolId = "smooth-ink-aa";
            ev.seed = req.seed;
            ev.color = makeColor(screenRgb[0], screenRgb[1], screenRgb[2], 1.0);
            ev.style.width = 2.6;
            ev.style.opacity = 0.75;
            ev.style.hardness = 1.0;
            ev.startUs = timeUs;
            for (const auto& pt : curve) {
                ev.geometry.push_back({pt.gx, pt.gy, pt.pressure, {}, {}, timeUs, 0, StrokeCapabilityLinearOptical});
                timeUs += 100;
            }
            ev.endUs = ev.geometry.back().timestampUs;
            timeUs += 50;

            TileData trialHi = hiLayerRoi;
            auto rOk = rasterizeStrokeIntoRoi(trialHi, rx0, ry0, roiMask, ev, req.stop);
            if (!rOk) continue;

            auto bBox = strokeBounds(ev).value();
            const int bx0 = std::max(0, int(bBox.x) - rx0), by0 = std::max(0, int(bBox.y) - ry0);
            const int bx1 = std::min(rw, int(bBox.x + std::int64_t(bBox.width)) - rx0);
            const int by1 = std::min(rh, int(bBox.y + std::int64_t(bBox.height)) - ry0);
            double gain = 0.0;
            for (int yy = by0; yy < by1; ++yy) {
                for (int xx = bx0; xx < bx1; ++xx) {
                    if (!roiMask[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)]) continue;
                    const std::size_t off = std::size_t(yy) * baseBeforeHi.rowStride + std::size_t(xx) * 4;
                    const std::size_t sOff = std::size_t(ry0 + yy) * src.rowStride + std::size_t(rx0 + xx) * 4;
                    std::uint8_t cand[4]{baseBeforeHi.bytes[off], baseBeforeHi.bytes[off + 1], baseBeforeHi.bytes[off + 2], baseBeforeHi.bytes[off + 3]};
                    compositeRgba8Pixel(cand, trialHi.bytes.data() + off, HiLayerOpacity, BlendMode::Screen, false);
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double dOld = double(liveComposite.bytes[off + c]) - double(src.bytes[sOff + c]);
                        const double dNew = double(cand[c]) - double(src.bytes[sOff + c]);
                        gain += dOld * dOld - dNew * dNew;
                    }
                }
            }
            if (gain < 64.0) continue;

            std::swap(hiLayerRoi, trialHi);
            for (int yy = by0; yy < by1; ++yy) {
                for (int xx = bx0; xx < bx1; ++xx) {
                    if (!roiMask[std::size_t(yy) * std::size_t(rw) + std::size_t(xx)]) continue;
                    const std::size_t off = std::size_t(yy) * baseBeforeHi.rowStride + std::size_t(xx) * 4;
                    std::uint8_t cand[4]{baseBeforeHi.bytes[off], baseBeforeHi.bytes[off + 1], baseBeforeHi.bytes[off + 2], baseBeforeHi.bytes[off + 3]};
                    compositeRgba8Pixel(cand, hiLayerRoi.bytes.data() + off, HiLayerOpacity, BlendMode::Screen, false);
                    std::copy_n(cand, 4, liveComposite.bytes.data() + off);
                }
            }

            StrokeEvent outEv = isSuperRes ? scaleContinuousEvent(std::move(ev), scaleX, scaleY, targetSize, false, {}) : std::move(ev);
            if (outEv.geometry.size() >= 2) {
                totalSamples += outEv.geometry.size();
                ++totalEvents;
                hiBatch.events.push_back(std::move(outEv));
            }
        }
    }

    ArtistObjectPlan plan;
    for (auto* b : {&flatsBatch, &glazeBatch, &structureBatch, &inkBatch, &hiBatch}) {
        if (!b->events.empty()) {
            auto checked = validateReconstructionBatch(*b);
            if (!checked) return checked.error();
            plan.batches.push_back(std::move(*b));
        }
    }
    if (plan.batches.empty())
        return error(ErrorCode::InvalidInput, "reconstruct.no-plannable-strokes");
    return plan;
}

} // namespace drawborad
