#include "PsdReader.h"
#include <cstring>

namespace drawborad {
namespace {

std::uint16_t u16(const std::uint8_t* p) {
    return std::uint16_t((p[0] << 8) | p[1]);
}

std::uint32_t u32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint64_t u64(const std::uint8_t* p) {
    return (std::uint64_t(u32(p)) << 32) | u32(p + 4);
}

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position{};

    bool take(std::size_t count) {
        if (position > bytes.size() || count > bytes.size() - position)
            return false;
        position += count;
        return true;
    }
};

} // namespace

Result<PhotoshopInfo> readPhotoshopHeader(std::span<const std::uint8_t> bytes,
                                          const CodecLimits& limits) {
    if (bytes.size() < 26 || std::memcmp(bytes.data(), "8BPS", 4) != 0)
        return error(ErrorCode::CorruptData, "psd.signature");

    const auto version = u16(bytes.data() + 4);
    if (version != 1 && version != 2)
        return error(ErrorCode::UnsupportedFeature, "psd.version");

    PhotoshopInfo out;
    out.format = version == 1 ? ImageFormat::Psd : ImageFormat::Psb;
    out.channels = u16(bytes.data() + 12);
    out.size = {u32(bytes.data() + 18), u32(bytes.data() + 14)};
    out.depth = u16(bytes.data() + 22);
    out.colorMode = u16(bytes.data() + 24);

    if (!out.size.width || !out.size.height || out.channels < 1 || out.channels > 56 ||
        (out.depth != 8 && out.depth != 16))
        return error(ErrorCode::CorruptData, "psd.header-values");

    Reader reader{bytes, 26};

    for (int section = 0; section < 2; ++section) {
        if (!reader.take(4))
            return error(ErrorCode::CorruptData, "psd.section-length");
        const auto length = u32(bytes.data() + reader.position - 4);
        if (length > bytes.size() - reader.position || !reader.take(length))
            return error(ErrorCode::CorruptData, "psd.section-bounds");
    }

    const std::size_t lengthBytes = version == 1 ? 4u : 8u;
    if (!reader.take(lengthBytes))
        return error(ErrorCode::CorruptData, "psd.layer-mask-length");

    const auto total = version == 1 ? std::uint64_t(u32(bytes.data() + reader.position - lengthBytes))
                                    : u64(bytes.data() + reader.position - lengthBytes);
    if (total > bytes.size() - reader.position)
        return error(ErrorCode::CorruptData, "psd.layer-mask-bounds");
    if (total == 0)
        return out;

    const auto sectionEnd = reader.position + std::size_t(total);

    if (!reader.take(lengthBytes))
        return error(ErrorCode::CorruptData, "psd.layer-info-length");

    const auto layerLength =
        version == 1 ? std::uint64_t(u32(bytes.data() + reader.position - lengthBytes))
                     : u64(bytes.data() + reader.position - lengthBytes);
    if (layerLength > sectionEnd - reader.position)
        return error(ErrorCode::CorruptData, "psd.layer-info-bounds");
    if (layerLength == 0)
        return out;

    if (!reader.take(2))
        return error(ErrorCode::CorruptData, "psd.layer-count");

    const auto signedCount = std::int16_t(u16(bytes.data() + reader.position - 2));
    const int layerCount = signedCount < 0 ? -signedCount : signedCount;
    if (std::uint64_t(layerCount) > limits.nodes)
        return error(ErrorCode::BudgetExceeded, "psd.layer-count-limit");

    for (int index = 0; index < layerCount; ++index) {
        if (reader.position + 18 > sectionEnd)
            return error(ErrorCode::CorruptData, "psd.layer-record");

        PhotoshopLayerInfo layer;
        layer.top = std::int32_t(u32(bytes.data() + reader.position));
        layer.left = std::int32_t(u32(bytes.data() + reader.position + 4));
        layer.bottom = std::int32_t(u32(bytes.data() + reader.position + 8));
        layer.right = std::int32_t(u32(bytes.data() + reader.position + 12));
        reader.position += 16;

        layer.channels = u16(bytes.data() + reader.position);
        reader.position += 2;
        if (layer.channels > 56)
            return error(ErrorCode::CorruptData, "psd.channel-count");

        const std::size_t descriptorSize = version == 1 ? 6u : 10u;
        const auto channelRecords = std::uint64_t(layer.channels) * descriptorSize;
        if (channelRecords > sectionEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.channel-records");
        reader.position += std::size_t(channelRecords);

        if (reader.position + 16 > sectionEnd)
            return error(ErrorCode::CorruptData, "psd.layer-blend");
        if (std::memcmp(bytes.data() + reader.position, "8BIM", 4) != 0)
            return error(ErrorCode::CorruptData, "psd.blend-signature");
        const auto* key = bytes.data() + reader.position + 4;
        if (std::memcmp(key, "norm", 4) == 0) layer.blend = BlendMode::Normal;
        else if (std::memcmp(key, "mul ", 4) == 0) layer.blend = BlendMode::Multiply;
        else if (std::memcmp(key, "scrn", 4) == 0) layer.blend = BlendMode::Screen;
        else if (std::memcmp(key, "dark", 4) == 0) layer.blend = BlendMode::Darken;
        else if (std::memcmp(key, "lite", 4) == 0) layer.blend = BlendMode::Lighten;
        else return error(ErrorCode::UnsupportedFeature, "psd.blend-mode");

        reader.position += 8;
        layer.opacity = bytes[reader.position];
        reader.position += 4;

        const auto extra = u32(bytes.data() + reader.position);
        reader.position += 4;
        if (extra > sectionEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.extra");
        reader.position += extra;
        out.layers.push_back(layer);
    }

    return out;
}

} // namespace drawborad
