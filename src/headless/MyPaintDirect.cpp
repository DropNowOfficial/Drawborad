#include "MyPaintDirect.h"
#include "codecs/RasterWriters.h"
#include "core/Digest.h"
#include "core/MyPaintCroppedRaster.h"
#include "storage/FileIO.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>

#ifdef DRAWBORAD_HAS_LIBMYPAINT_HEADERS
#include <mypaint-brush.h>
#include <mypaint-fixed-tiled-surface.h>
#endif

namespace drawborad {
namespace {

struct AbiRect {
    int x, y, width, height;
};

struct AbiTileRequest {
    int tx;
    int ty;
    int readonly_flag;
    std::uint16_t* buffer;
    void* context;
    int thread_id;
    int mipmap_level;
};

#ifdef DRAWBORAD_HAS_LIBMYPAINT_HEADERS
static_assert(sizeof(AbiRect) == sizeof(MyPaintRectangle));
static_assert(sizeof(AbiTileRequest) == sizeof(MyPaintTileRequest));
static_assert(offsetof(AbiTileRequest, buffer) == offsetof(MyPaintTileRequest, buffer));
static_assert(offsetof(AbiTileRequest, mipmap_level) == offsetof(MyPaintTileRequest, mipmap_level));
#endif

struct MyPaintCAbi {
    void* handle{};
    void* (*fixed_new)(int, int){};
    void* (*fixed_interface)(void*){};
    void (*tile_req_init)(AbiTileRequest*, int, int, int, int){};
    void (*tile_req_start)(void*, AbiTileRequest*){};
    void (*tile_req_end)(void*, AbiTileRequest*){};
    void (*begin_atomic)(void*){};
    void (*end_atomic)(void*, AbiRect*){};
    void* (*brush_new)(){};
    int (*brush_from_string)(void*, const char*){};
    int (*brush_stroke_to)(void*, void*, float, float, float, float, float, double){};
    void (*brush_new_stroke)(void*){};
    void (*brush_reset)(void*){};
    void (*brush_unref)(void*){};
    void (*surface_unref)(void*){};
    int (*setting_from_cname)(const char*){};
    void (*set_base_value)(void*, int, float){};
    float (*get_base_value)(void*, int){};
    bool ok{};
};

template <class Fn>
bool loadSym(void* handle, const char* name, Fn& out) noexcept {
    static_assert(sizeof(Fn) == sizeof(void*));
    void* raw = dlsym(handle, name);
    if (!raw) return false;
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

const MyPaintCAbi& runtimeAbi() noexcept {
    static const MyPaintCAbi abi = []() noexcept {
        MyPaintCAbi a{};
        const char* candidates[] = {
            "libmypaint-1.5.so.1",
            "/lib/x86_64-linux-gnu/libmypaint-1.5.so.1",
            "/usr/lib/x86_64-linux-gnu/libmypaint-1.5.so.1",
            "libmypaint.so"
        };
        for (const char* soname : candidates) {
            a.handle = dlopen(soname, RTLD_LOCAL | RTLD_NOW);
            if (a.handle) break;
        }
        if (!a.handle) return a;
        a.ok = loadSym(a.handle, "mypaint_fixed_tiled_surface_new", a.fixed_new) &&
               loadSym(a.handle, "mypaint_fixed_tiled_surface_interface", a.fixed_interface) &&
               loadSym(a.handle, "mypaint_tile_request_init", a.tile_req_init) &&
               loadSym(a.handle, "mypaint_tiled_surface_tile_request_start", a.tile_req_start) &&
               loadSym(a.handle, "mypaint_tiled_surface_tile_request_end", a.tile_req_end) &&
               loadSym(a.handle, "mypaint_surface_begin_atomic", a.begin_atomic) &&
               loadSym(a.handle, "mypaint_surface_end_atomic", a.end_atomic) &&
               loadSym(a.handle, "mypaint_brush_new", a.brush_new) &&
               loadSym(a.handle, "mypaint_brush_from_string", a.brush_from_string) &&
               loadSym(a.handle, "mypaint_brush_stroke_to", a.brush_stroke_to) &&
               loadSym(a.handle, "mypaint_brush_new_stroke", a.brush_new_stroke) &&
               loadSym(a.handle, "mypaint_brush_reset", a.brush_reset) &&
               loadSym(a.handle, "mypaint_brush_unref", a.brush_unref) &&
               loadSym(a.handle, "mypaint_surface_unref", a.surface_unref) &&
               loadSym(a.handle, "mypaint_brush_setting_from_cname", a.setting_from_cname) &&
               loadSym(a.handle, "mypaint_brush_set_base_value", a.set_base_value) &&
               loadSym(a.handle, "mypaint_brush_get_base_value", a.get_base_value);
        return a;
    }();
    return abi;
}

std::filesystem::path findPresetDirectory() {
    std::error_code ec;
#ifdef DRAWBORAD_SOURCE_ROOT
    const std::filesystem::path compiled = std::filesystem::path(DRAWBORAD_SOURCE_ROOT) / "cloud/mypaint_data";
    if (std::filesystem::is_directory(compiled, ec)) return compiled;
#endif
    const auto cwd = std::filesystem::current_path(ec) / "cloud/mypaint_data";
    if (!ec && std::filesystem::is_directory(cwd, ec)) return cwd;
    return {};
}

std::mutex& abiMutex() noexcept {
    static std::mutex m;
    return m;
}

} // namespace

bool mypaintDirectAvailable() noexcept {
    if (!runtimeAbi().ok) return false;
    const auto dir = findPresetDirectory();
    std::error_code ec;
    return !dir.empty() && std::filesystem::is_regular_file(dir / "pointy_ink.myb", ec);
}

Result<Command> mypaintDirectStrokeCommand(
    const DocumentSnapshot& snapshot,
    const std::string& presetName,
    std::span<const MyPaintDirectSample> points,
    std::array<std::uint8_t, 3> rgb,
    double radiusScale,
    bool normalMode,
    NodeId eventId) {
    if (presetName != "pointy_ink" && presetName != "dry_brush")
        return error(ErrorCode::UnsupportedFeature, "mypaint-direct.preset");
    if (points.empty() || points.size() > 1024)
        return error(ErrorCode::InvalidInput, "mypaint-direct.points");
    if (!std::isfinite(radiusScale) || radiusScale < 0.25 || radiusScale > 8.0)
        return error(ErrorCode::InvalidInput, "mypaint-direct.scale");
    const auto& abi = runtimeAbi();
    if (!abi.ok)
        return error(ErrorCode::UnsupportedFeature, "mypaint-direct.unavailable");
    const auto presetDir = findPresetDirectory();
    if (presetDir.empty())
        return error(ErrorCode::IoError, "mypaint-direct.preset-dir");
    auto presetBytes = NativeFileIO{}.read(presetDir / (presetName + ".myb"), 256 * 1024);
    if (!presetBytes) return presetBytes.error();
    std::string presetText(reinterpret_cast<const char*>(presetBytes.value().data()),
                           presetBytes.value().size());

    const auto cw = snapshot.spec.pixelSize.width;
    const auto ch = snapshot.spec.pixelSize.height;
    if (cw == 0 || ch == 0 || cw > 8192 || ch > 8192 || cw * ch > 20000000)
        return error(ErrorCode::BudgetExceeded, "mypaint-direct.canvas");

    double minX = points.front().x, maxX = minX;
    double minY = points.front().y, maxY = minY;
    double lastTime = -1.0;
    for (const auto& p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.pressure) ||
            !std::isfinite(p.timeMs) || p.x < 0 || p.y < 0 || p.x >= double(cw) ||
            p.y >= double(ch) || p.pressure < 0 || p.pressure > 1 ||
            p.timeMs < 0 || p.timeMs > 60000 || p.timeMs < lastTime)
            return error(ErrorCode::InvalidInput, "mypaint-direct.sample");
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        lastTime = p.timeMs;
    }

    const std::int64_t pad = 128;
    const std::int64_t ox = std::max<std::int64_t>(0, std::int64_t(std::floor(minX)) - pad);
    const std::int64_t oy = std::max<std::int64_t>(0, std::int64_t(std::floor(minY)) - pad);
    const std::int64_t rx1 = std::min<std::int64_t>(std::int64_t(cw), std::int64_t(std::ceil(maxX)) + pad + 1);
    const std::int64_t ry1 = std::min<std::int64_t>(std::int64_t(ch), std::int64_t(std::ceil(maxY)) + pad + 1);
    const auto rw = std::uint64_t(rx1 - ox);
    const auto rh = std::uint64_t(ry1 - oy);
    if (rw == 0 || rh == 0 || rw > 2048 || rh > 2048 || rw * rh > 1048576)
        return error(ErrorCode::BudgetExceeded, "mypaint-direct.roi-budget");

    Bytes rgba(std::size_t(rw * rh * 4), 0);
    {
        std::lock_guard<std::mutex> guard(abiMutex());
        void* surface = abi.fixed_new(int(rw), int(rh));
        if (!surface) return error(ErrorCode::BudgetExceeded, "mypaint-direct.surface");
        void* iface = abi.fixed_interface(surface);
        const int nx = int((rw + 63) / 64), ny = int((rh + 63) / 64);
        for (int ty = 0; ty < ny; ++ty) {
            for (int tx = 0; tx < nx; ++tx) {
                AbiTileRequest req{};
                abi.tile_req_init(&req, 0, tx, ty, 0);
                abi.tile_req_start(surface, &req);
                if (req.buffer) std::fill_n(req.buffer, 64 * 64 * 4, std::uint16_t(32768));
                abi.tile_req_end(surface, &req);
            }
        }
        void* brush = abi.brush_new();
        if (!brush || abi.brush_from_string(brush, presetText.c_str()) != 1) {
            if (brush) abi.brush_unref(brush);
            abi.surface_unref(iface);
            return error(ErrorCode::CorruptData, "mypaint-direct.preset-load");
        }
        if (radiusScale != 1.0) {
            const int radSetting = abi.setting_from_cname("radius_logarithmic");
            const float base = abi.get_base_value(brush, radSetting);
            abi.set_base_value(brush, radSetting, float(double(base) + std::log(radiusScale)));
        }
        for (const char* chName : {"color_h", "color_s", "color_v"}) {
            abi.set_base_value(brush, abi.setting_from_cname(chName), 0.0f);
        }
        abi.brush_reset(brush);
        abi.brush_new_stroke(brush);
        abi.begin_atomic(iface);
        const auto& p0 = points.front();
        abi.brush_stroke_to(brush, iface, float(p0.x - double(ox)), float(p0.y - double(oy)), 0.0f, 0.0f, 0.0f, 10.0);
        double prevMs = p0.timeMs;
        double totalArc = 0.0;
        double maxPress = p0.pressure;
        for (std::size_t idx = 0; idx < points.size(); ++idx) {
            const auto& p = points[idx];
            if (idx > 0) totalArc += std::hypot(p.x - points[idx - 1].x, p.y - points[idx - 1].y);
            maxPress = std::max(maxPress, p.pressure);
            const double dt = std::max(0.001, (p.timeMs - prevMs) / 1000.0);
            abi.brush_stroke_to(brush, iface, float(p.x - double(ox)), float(p.y - double(oy)), float(p.pressure), 0.0f, 0.0f, dt);
            prevMs = p.timeMs;
        }
        const auto& plast = points.back();
        if (totalArc < 0.35 && maxPress > 0.0) {
            // Presets driven by dabs_per_actual_radius require non-zero arc length to emit a dab on 1-point taps.
            const double step = (plast.x + 0.55 < double(cw) && plast.x - double(ox) + 0.55 < double(rw)) ? 0.55
                              : (plast.x - 0.55 >= 0.0 && plast.x - double(ox) >= 0.55) ? -0.55 : 0.0;
            const double stepY = (step == 0.0 && plast.y + 0.55 < double(ch) && plast.y - double(oy) + 0.55 < double(rh)) ? 0.55 : 0.0;
            abi.brush_stroke_to(brush, iface, float(plast.x - double(ox) + step),
                                float(plast.y - double(oy) + stepY), float(maxPress), 0.0f, 0.0f, 0.015);
            abi.brush_stroke_to(brush, iface, float(plast.x - double(ox) + step),
                                float(plast.y - double(oy) + stepY), 0.0f, 0.0f, 0.0f, 0.02);
        } else {
            abi.brush_stroke_to(brush, iface, float(plast.x - double(ox)), float(plast.y - double(oy)), 0.0f, 0.0f, 0.0f, 0.02);
        }
        AbiRect dirty{};
        abi.end_atomic(iface, &dirty);
        abi.brush_unref(brush);

        for (int ty = 0; ty < ny; ++ty) {
            for (int tx = 0; tx < nx; ++tx) {
                AbiTileRequest req{};
                abi.tile_req_init(&req, 0, tx, ty, 1);
                abi.tile_req_start(surface, &req);
                const int tileH = std::min(64, int(rh) - ty * 64);
                const int tileW = std::min(64, int(rw) - tx * 64);
                for (int y = 0; y < tileH; ++y) {
                    for (int x = 0; x < tileW; ++x) {
                        const std::size_t dst = (std::size_t(ty * 64 + y) * rw + std::size_t(tx * 64 + x)) * 4;
                        const std::size_t src = std::size_t(y * 64 + x) * 4;
                        const int gray = std::clamp(int(std::lround(double(req.buffer[src]) * 255.0 / 32768.0)), 0, 255);
                        rgba[dst + 0] = rgb[0];
                        rgba[dst + 1] = rgb[1];
                        rgba[dst + 2] = rgb[2];
                        rgba[dst + 3] = std::uint8_t(255 - gray);
                    }
                }
                abi.tile_req_end(surface, &req);
            }
        }
        abi.surface_unref(iface);
    }

    bool hasInk = false;
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] != 0) { hasInk = true; break; }
    }
    if (!hasInk) return error(ErrorCode::InvalidInput, "mypaint-direct.no-visible-ink");

    TileData tile;
    tile.width = std::uint32_t(rw);
    tile.height = std::uint32_t(rh);
    tile.rowStride = rw * 4;
    tile.format = {ColorMode::RGB, Depth::U8, true};
    tile.bytes = rgba;
    RasterEncodingOptions encOpts;
    encOpts.ppi = snapshot.spec.ppi;
    auto png = encodePngBounded(tile, encOpts, 16 * 1024 * 1024);
    if (!png) return png.error();

    QJsonArray globalPts;
    for (const auto& p : points) {
        globalPts.append(QJsonObject{{"x", p.x}, {"y", p.y}, {"pressure", p.pressure}, {"time_ms", p.timeMs}});
    }
    QJsonObject meta{
        {"prototype", "mypaint-c-direct-v1"},
        {"preset", QString::fromStdString(presetName)},
        {"preset_sha256", QString::fromStdString(sha256(presetBytes.value()).hex())},
        {"paint_mode", normalMode ? "normal" : "multiply"},
        {"radius_scale", radiusScale},
        {"origin", QJsonArray{qint64(ox), qint64(oy)}},
        {"roi_size", QJsonArray{qint64(rw), qint64(rh)}},
        {"full_canvas", QJsonArray{qint64(cw), qint64(ch)}},
        {"png_sha256", QString::fromStdString(sha256(png.value()).hex())},
        {"points", globalPts}
    };
    const auto jsonBytes = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    Bytes manifestBytes(reinterpret_cast<const std::uint8_t*>(jsonBytes.constData()),
                        reinterpret_cast<const std::uint8_t*>(jsonBytes.constData()) + jsonBytes.size());
    Rect roi{ox, oy, rw, rh};
    return normalMode
        ? croppedRaster::makeNormal(std::move(rgba), std::move(png.value()), std::move(manifestBytes), roi, cw, ch, eventId, {})
        : croppedRaster::make(std::move(rgba), std::move(png.value()), std::move(manifestBytes), roi, cw, ch, eventId, {});
}

} // namespace drawborad
