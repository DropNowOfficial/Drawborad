#include "TileStore.h"
#include "Binary.h"
#include "core/Digest.h"
namespace drawborad {
namespace {
Result<void> valid(const TileData& t) {
    if (!t.width || !t.height || t.width > 512 || t.height > 512 ||
        (t.format.mode != ColorMode::RGB && t.format.mode != ColorMode::CMYK) ||
        (t.format.depth != Depth::U8 && t.format.depth != Depth::U16) || t.engineVersion != 1)
        return error(ErrorCode::InvalidInput, "tile.layout");
    if (t.format.mode == ColorMode::CMYK && t.profileId.empty())
        return error(ErrorCode::ProfileRequired, "tile.profile");
    auto stride =
        std::uint64_t(t.width) * t.format.channelCount() * (t.format.depth == Depth::U16 ? 2 : 1);
    auto bytes = checkedMultiply(t.rowStride, t.height);
    if (!bytes)
        return bytes.error();
    if (t.rowStride < stride || t.rowStride > 512 * 10 + 4096 || bytes.value() != t.bytes.size())
        return error(ErrorCode::InvalidInput, "tile.stride");
    return {};
}
} // namespace
Result<TileId> TileStore::put(const TileData& t) {
    auto v = valid(t);
    if (!v)
        return v.error();
    binary::Writer w;
    w.text("DTILE1");
    w.u64(std::uint64_t(t.format.mode));
    w.u64(std::uint64_t(t.format.depth));
    w.boolean(t.format.alpha);
    w.u64(t.width);
    w.u64(t.height);
    w.u64(t.rowStride);
    w.hash(t.profileId);
    w.u64(t.engineVersion);
    w.blob(t.bytes);
    auto id = sha256(w.data);
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec)
        return error(ErrorCode::IoError, "tile.mkdir");
    auto p = confinedPath(root_, id.hex() + ".tile");
    if (!p)
        return p.error();
    if (std::filesystem::exists(p.value())) {
        auto old = io_->read(p.value(), 16 * 1024 * 1024);
        if (!old)
            return old.error();
        if (sha256(old.value()) != id)
            return error(ErrorCode::DigestMismatch, "tile.existing-corrupt");
        return id;
    }
    auto r = atomicWrite(*io_, p.value(), w.data);
    if (!r)
        return r.error();
    return id;
}
Result<TileData> TileStore::get(TileId id) const {
    auto p = confinedPath(root_, id.hex() + ".tile");
    if (!p)
        return p.error();
    auto b = io_->read(p.value(), 16 * 1024 * 1024);
    if (!b)
        return b.error();
    if (sha256(b.value()) != id)
        return error(ErrorCode::DigestMismatch, "tile.integrity");
    try {
        binary::Reader r{b.value()};
        if (r.text() != "DTILE1")
            throw binary::Invalid{};
        TileData t;
        t.format.mode = ColorMode(r.u64());
        t.format.depth = Depth(r.u64());
        t.format.alpha = r.boolean();
        auto wi = r.u64(), he = r.u64();
        if (wi > 512 || he > 512)
            throw binary::Invalid{};
        t.width = std::uint32_t(wi);
        t.height = std::uint32_t(he);
        t.rowStride = r.u64();
        t.profileId = r.hash();
        auto version = r.u64();
        if (version != 1)
            throw binary::Invalid{};
        t.engineVersion = 1;
        t.bytes = r.blob(16 * 1024 * 1024);
        if (!r.done())
            throw binary::Invalid{};
        auto v = valid(t);
        if (!v)
            return v.error();
        return t;
    } catch (const binary::Invalid&) {
        return error(ErrorCode::CorruptData, "tile.decode");
    }
}
} // namespace drawborad
