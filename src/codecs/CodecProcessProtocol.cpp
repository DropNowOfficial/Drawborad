#include "CodecProcessProtocol.h"
#include "storage/Binary.h"
namespace drawborad {
namespace {
Result<void> validateTile(const TileData& t, const CodecLimits& limits) {
    if (!t.width || !t.height || !t.rowStride)
        return error(ErrorCode::CorruptData, "codec.payload-layout");
    if (t.format.mode != ColorMode::RGB && t.format.mode != ColorMode::CMYK)
        return error(ErrorCode::CorruptData, "codec.payload-format");
    if (t.format.depth != Depth::U8 && t.format.depth != Depth::U16)
        return error(ErrorCode::CorruptData, "codec.payload-depth");
    auto minimumRow = checkedMultiply(
        std::uint64_t(t.width),
        std::uint64_t(t.format.channelCount()) * (t.format.depth == Depth::U16 ? 2ull : 1ull));
    if (!minimumRow || t.rowStride < minimumRow.value())
        return error(ErrorCode::CorruptData, "codec.payload-stride");
    auto required = checkedMultiply(t.rowStride, t.height);
    if (!required || required.value() != t.bytes.size())
        return error(ErrorCode::CorruptData, "codec.payload-size");
    if (t.bytes.size() > limits.decodedBytes)
        return error(ErrorCode::BudgetExceeded, "codec.payload-budget");
    return {};
}
}
Result<Bytes> encodeCodecPayload(const CodecPayload& payload, const CodecLimits& limits) {
    auto valid = validateTile(payload.pixels, limits);
    if (!valid) return valid.error();
    binary::Writer w;
    w.text("DRAWBORAD-CODEC-1");
    w.u64(payload.info.originalSize.width);
    w.u64(payload.info.originalSize.height);
    w.u64(payload.info.orientedSize.width);
    w.u64(payload.info.orientedSize.height);
    w.u64(std::uint64_t(payload.info.format));
    w.hash(payload.info.icc);
    w.u64(payload.info.estimatedBytes);
    w.hash(payload.info.sourceHash);
    w.boolean(payload.info.hasAlpha);
    w.u64(payload.info.orientation);
    w.u64(payload.pixels.width);
    w.u64(payload.pixels.height);
    w.u64(std::uint64_t(payload.pixels.format.mode));
    w.u64(std::uint64_t(payload.pixels.format.depth));
    w.boolean(payload.pixels.format.alpha);
    w.u64(payload.pixels.rowStride);
    w.hash(payload.pixels.profileId);
    w.u64(payload.pixels.engineVersion);
    w.blob(payload.pixels.bytes);
    if (w.data.size() > limits.encodedBytes)
        return error(ErrorCode::BudgetExceeded, "codec.payload-encoded-budget");
    return w.data;
}
Result<CodecPayload> decodeCodecPayload(std::span<const std::uint8_t> bytes, const CodecLimits& limits) {
    if (bytes.size() > limits.encodedBytes)
        return error(ErrorCode::BudgetExceeded, "codec.payload-encoded-budget");
    try {
        binary::Reader r{bytes};
        if (r.text(64) != "DRAWBORAD-CODEC-1")
            return error(ErrorCode::CorruptData, "codec.payload-magic");
        CodecPayload out;
        out.info.originalSize = {r.u64(), r.u64()};
        out.info.orientedSize = {r.u64(), r.u64()};
        auto imageFormat = r.u64();
        if (imageFormat > std::uint64_t(ImageFormat::Unknown))
            return error(ErrorCode::CorruptData, "codec.payload-image-format");
        out.info.format = ImageFormat(imageFormat);
        out.info.icc = r.hash();
        out.info.estimatedBytes = r.u64();
        out.info.sourceHash = r.hash();
        out.info.hasAlpha = r.boolean();
        auto orientation = r.u64();
        if (orientation < 1 || orientation > 8)
            return error(ErrorCode::CorruptData, "codec.payload-orientation");
        out.info.orientation = std::uint16_t(orientation);
        auto width = r.u64(), height = r.u64();
        if (width > UINT32_MAX || height > UINT32_MAX)
            return error(ErrorCode::BudgetExceeded, "codec.payload-dimensions");
        out.pixels.width = std::uint32_t(width);
        out.pixels.height = std::uint32_t(height);
        auto mode = r.u64(), depth = r.u64();
        if (mode > std::uint64_t(ColorMode::CMYK) ||
            (depth != std::uint64_t(Depth::U8) && depth != std::uint64_t(Depth::U16)))
            return error(ErrorCode::CorruptData, "codec.payload-format");
        out.pixels.format.mode = ColorMode(mode);
        out.pixels.format.depth = Depth(depth);
        out.pixels.format.alpha = r.boolean();
        out.pixels.rowStride = r.u64();
        out.pixels.profileId = r.hash();
        auto engineVersion = r.u64();
        if (engineVersion > UINT32_MAX)
            return error(ErrorCode::CorruptData, "codec.payload-engine");
        out.pixels.engineVersion = std::uint32_t(engineVersion);
        out.pixels.bytes = r.blob(std::size_t(std::min<std::uint64_t>(
            limits.decodedBytes, std::uint64_t(SIZE_MAX))));
        if (!r.done())
            return error(ErrorCode::CorruptData, "codec.payload-trailing");
        auto valid = validateTile(out.pixels, limits);
        if (!valid) return valid.error();
        return out;
    } catch (const binary::Invalid&) {
        return error(ErrorCode::CorruptData, "codec.payload-truncated");
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "codec.payload-allocation");
    }
}
}
