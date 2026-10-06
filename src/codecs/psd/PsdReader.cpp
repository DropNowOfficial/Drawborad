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

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) {
        out.push_back(char(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(char(0xc0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(char(0xe0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(char(0xf0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    }
}

Result<std::string> decodeUnicodeName(std::span<const std::uint8_t> data) {
    if (data.size() < 4)
        return error(ErrorCode::CorruptData, "psd.luni-length");
    const auto count = std::uint64_t(u32(data.data()));
    if (count > (data.size() - 4) / 2)
        return error(ErrorCode::CorruptData, "psd.luni-bounds");
    std::string out;
    out.reserve(std::size_t(count));
    std::size_t at = 4;
    for (std::uint64_t i = 0; i < count; ++i, at += 2) {
        auto unit = std::uint32_t(u16(data.data() + at));
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i >= count || at + 4 > data.size())
                return error(ErrorCode::CorruptData, "psd.luni-surrogate");
            const auto low = std::uint32_t(u16(data.data() + at + 2));
            if (low < 0xdc00 || low > 0xdfff)
                return error(ErrorCode::CorruptData, "psd.luni-surrogate");
            const auto cp = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
            appendUtf8(out, cp);
            at += 2;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return error(ErrorCode::CorruptData, "psd.luni-surrogate");
        } else {
            appendUtf8(out, unit);
        }
    }
    return out;
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

Result<void> readLayerExtra(std::span<const std::uint8_t> bytes, std::size_t begin,
                            std::size_t end, PhotoshopLayerInfo& layer) {
    if (begin > end || end > bytes.size())
        return error(ErrorCode::CorruptData, "psd.extra-bounds");
    auto at = begin;
    if (end - at < 4)
        return error(ErrorCode::CorruptData, "psd.mask-length");
    const auto maskLength = std::uint64_t(u32(bytes.data() + at));
    at += 4;
    if (maskLength > end - at)
        return error(ErrorCode::CorruptData, "psd.mask-bounds");
    at += std::size_t(maskLength);

    if (end - at < 4)
        return error(ErrorCode::CorruptData, "psd.blend-range-length");
    const auto blendLength = std::uint64_t(u32(bytes.data() + at));
    at += 4;
    if (blendLength > end - at)
        return error(ErrorCode::CorruptData, "psd.blend-range-bounds");
    at += std::size_t(blendLength);

    if (at >= end)
        return error(ErrorCode::CorruptData, "psd.layer-name");
    const auto pascalLength = std::size_t(bytes[at]);
    const auto pascalBytes = std::size_t(1) + pascalLength;
    const auto paddedPascal = (pascalBytes + 3u) & ~std::size_t(3u);
    if (paddedPascal > end - at)
        return error(ErrorCode::CorruptData, "psd.layer-name-bounds");
    layer.name.assign(reinterpret_cast<const char*>(bytes.data() + at + 1), pascalLength);
    at += paddedPascal;

    while (end - at >= 12) {
        const auto* signature = bytes.data() + at;
        if (std::memcmp(signature, "8BIM", 4) != 0 &&
            std::memcmp(signature, "8B64", 4) != 0)
            break;
        const auto* key = signature + 4;
        const auto blockLength = std::uint64_t(u32(signature + 8));
        at += 12;
        if (blockLength > end - at)
            return error(ErrorCode::CorruptData, "psd.additional-info-bounds");
        if (std::memcmp(key, "luni", 4) == 0) {
            auto decoded = decodeUnicodeName(bytes.subspan(at, std::size_t(blockLength)));
            if (!decoded)
                return decoded.error();
            layer.name = std::move(decoded.value());
        }
        at += std::size_t(blockLength);
        if ((blockLength & 1u) != 0) {
            if (at >= end)
                return error(ErrorCode::CorruptData, "psd.additional-info-padding");
            ++at;
        }
    }
    return {};
}

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
    const std::uint64_t dimensionLimit = version == 1 ? 30000ull : 300000ull;
    if (out.size.width > dimensionLimit || out.size.height > dimensionLimit)
        return error(ErrorCode::UnsupportedFeature, "psd.dimension-limit");

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

        const auto extra = std::uint64_t(u32(bytes.data() + reader.position));
        reader.position += 4;
        if (extra > sectionEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.extra");
        const auto extraBegin = reader.position;
        const auto extraEnd = extraBegin + std::size_t(extra);
        auto parsedExtra = readLayerExtra(bytes, extraBegin, extraEnd, layer);
        if (!parsedExtra)
            return parsedExtra.error();
        reader.position = extraEnd;
        out.layers.push_back(std::move(layer));
    }

    return out;
}

} // namespace drawborad
