#include "PsdReader.h"
#include "storage/FileIO.h"
#include <algorithm>
#include <array>
#include <cstring>
#ifdef DRAWBORAD_HAS_ZLIB
#include <zlib.h>
#endif

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

Result<void> readImageResources(std::span<const std::uint8_t> bytes,
                                std::size_t begin, std::size_t end,
                                PhotoshopInfo& out) {
    if (begin > end || end > bytes.size())
        return error(ErrorCode::CorruptData, "psd.image-resources-bounds");
    auto at = begin;
    while (at < end) {
        if (end - at < 12)
            return error(ErrorCode::CorruptData, "psd.image-resource-header");
        if (std::memcmp(bytes.data() + at, "8BIM", 4) != 0)
            return error(ErrorCode::CorruptData, "psd.image-resource-signature");
        const auto id = u16(bytes.data() + at + 4);
        at += 6;
        const auto nameLength = std::size_t(bytes[at]);
        const auto nameBytes = std::size_t(1) + nameLength;
        const auto paddedName = (nameBytes + 1u) & ~std::size_t(1u);
        if (paddedName > end - at)
            return error(ErrorCode::CorruptData, "psd.image-resource-name");
        at += paddedName;
        if (end - at < 4)
            return error(ErrorCode::CorruptData, "psd.image-resource-length");
        const auto length = std::uint64_t(u32(bytes.data() + at));
        at += 4;
        if (length > end - at)
            return error(ErrorCode::CorruptData, "psd.image-resource-data");
        auto data = bytes.subspan(at, std::size_t(length));
        if (id == 1039) {
            out.iccProfile.assign(data.begin(), data.end());
        } else if (id == 1005) {
            if (data.size() < 16)
                return error(ErrorCode::CorruptData, "psd.resolution-info");
            const auto horizontal = u32(data.data());
            const auto vertical = u32(data.data() + 8);
            if (horizontal == 0 || vertical == 0)
                return error(ErrorCode::CorruptData, "psd.resolution-zero");
            out.ppi = (double(horizontal) + double(vertical)) / (2.0 * 65536.0);
        }
        at += std::size_t(length);
        if ((length & 1u) != 0) {
            if (at >= end)
                return error(ErrorCode::CorruptData, "psd.image-resource-padding");
            ++at;
        }
    }
    return {};
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

struct LayerInfoLocation {
    std::size_t begin{};
    std::size_t end{};
};

std::uint64_t sizedLength(std::span<const std::uint8_t> bytes, std::size_t at,
                          std::size_t count) {
    return count == 4 ? std::uint64_t(u32(bytes.data() + at))
                      : u64(bytes.data() + at);
}

bool psbEightByteAdditionalLength(std::span<const std::uint8_t> key) {
    if (key.size() != 4)
        return false;
    static constexpr std::array<std::array<char, 4>, 13> keys{{
        {{'L','M','s','k'}}, {{'L','r','1','6'}}, {{'L','r','3','2'}}, {{'L','a','y','r'}},
        {{'M','t','1','6'}}, {{'M','t','3','2'}}, {{'M','t','r','n'}}, {{'A','l','p','h'}},
        {{'F','M','s','k'}}, {{'l','n','k','2'}}, {{'F','E','i','d'}}, {{'F','X','i','d'}},
        {{'P','x','S','D'}}
    }};
    for (const auto& candidate : keys)
        if (std::memcmp(key.data(), candidate.data(), 4) == 0)
            return true;
    return false;
}

Result<std::optional<LayerInfoLocation>> locateLayerInfo(
    std::span<const std::uint8_t> bytes, std::size_t sectionBegin,
    std::size_t sectionEnd, std::uint16_t version, std::uint16_t depth) {
    if (sectionBegin > sectionEnd || sectionEnd > bytes.size())
        return error(ErrorCode::CorruptData, "psd.layer-mask-bounds");
    const std::size_t mainLengthBytes = version == 1 ? 4u : 8u;
    if (mainLengthBytes > sectionEnd - sectionBegin)
        return error(ErrorCode::CorruptData, "psd.layer-info-length");

    auto at = sectionBegin;
    const auto mainLength = sizedLength(bytes, at, mainLengthBytes);
    at += mainLengthBytes;
    if (mainLength > sectionEnd - at)
        return error(ErrorCode::CorruptData, "psd.layer-info-bounds");

    std::optional<LayerInfoLocation> selected;
    if (mainLength != 0) {
        if (mainLength < 2)
            return error(ErrorCode::CorruptData, "psd.layer-info-bounds");
        selected = LayerInfoLocation{at, at + std::size_t(mainLength)};
    }
    at += std::size_t(mainLength);

    if (sectionEnd - at < 4)
        return error(ErrorCode::CorruptData, "psd.global-mask-length");
    const auto globalMaskLength = std::uint64_t(u32(bytes.data() + at));
    at += 4;
    if (globalMaskLength > sectionEnd - at)
        return error(ErrorCode::CorruptData, "psd.global-mask-bounds");
    at += std::size_t(globalMaskLength);

    while (at < sectionEnd) {
        if (sectionEnd - at < 12)
            return error(ErrorCode::CorruptData, "psd.additional-layer-info-header");
        const auto* signature = bytes.data() + at;
        if (std::memcmp(signature, "8BIM", 4) != 0 &&
            std::memcmp(signature, "8B64", 4) != 0)
            return error(ErrorCode::CorruptData, "psd.additional-layer-info-signature");
        const auto key = bytes.subspan(at + 4, 4);
        const bool longLength = version == 2 && psbEightByteAdditionalLength(key);
        const std::size_t lengthBytes = longLength ? 8u : 4u;
        if (sectionEnd - at < 8 + lengthBytes)
            return error(ErrorCode::CorruptData, "psd.additional-layer-info-length");
        const auto length = sizedLength(bytes, at + 8, lengthBytes);
        const auto payload = at + 8 + lengthBytes;
        if (length > sectionEnd - payload)
            return error(ErrorCode::CorruptData, "psd.additional-layer-info-bounds");

        if (std::memcmp(key.data(), "lnk2", 4) == 0 ||
            std::memcmp(key.data(), "lnkD", 4) == 0 ||
            std::memcmp(key.data(), "lnk3", 4) == 0 ||
            std::memcmp(key.data(), "lnkE", 4) == 0) {
            return Error{ErrorCode::UnsupportedFeature,
                         "psd.unsupported-document-feature",
                         std::string(reinterpret_cast<const char*>(key.data()), 4)};
        }

        if (depth == 16 && std::memcmp(key.data(), "Lr16", 4) == 0) {
            if (length < 2)
                return error(ErrorCode::CorruptData, "psd.lr16-length");
            selected = LayerInfoLocation{payload, payload + std::size_t(length)};
        }

        at = payload + std::size_t(length);
        if ((length & 1u) != 0) {
            if (at >= sectionEnd)
                return error(ErrorCode::CorruptData, "psd.additional-layer-info-padding");
            ++at;
        }
    }
    return selected;
}

Result<void> readLayerExtra(std::span<const std::uint8_t> bytes, std::size_t begin,
                            std::size_t end, std::uint16_t version,
                            PhotoshopLayerInfo& layer) {
    if (begin > end || end > bytes.size())
        return error(ErrorCode::CorruptData, "psd.extra-bounds");
    auto at = begin;
    if (end - at < 4)
        return error(ErrorCode::CorruptData, "psd.mask-length");
    const auto maskLength = std::uint64_t(u32(bytes.data() + at));
    at += 4;
    if (maskLength > end - at)
        return error(ErrorCode::CorruptData, "psd.mask-bounds");
    if (maskLength != 0) {
        if (maskLength < 18)
            return error(ErrorCode::CorruptData, "psd.mask-record");
        const auto maskEnd = at + std::size_t(maskLength);
        layer.maskBounds = {
            std::int32_t(u32(bytes.data() + at + 4)),
            std::int32_t(u32(bytes.data() + at)),
            std::uint64_t(std::int64_t(std::int32_t(u32(bytes.data() + at + 12))) -
                          std::int64_t(std::int32_t(u32(bytes.data() + at + 4)))),
            std::uint64_t(std::int64_t(std::int32_t(u32(bytes.data() + at + 8))) -
                          std::int64_t(std::int32_t(u32(bytes.data() + at))))
        };
        const auto bottom = std::int32_t(u32(bytes.data() + at + 8));
        const auto right = std::int32_t(u32(bytes.data() + at + 12));
        if (bottom < std::int32_t(u32(bytes.data() + at)) ||
            right < std::int32_t(u32(bytes.data() + at + 4)))
            return error(ErrorCode::CorruptData, "psd.mask-rectangle");
        layer.maskDefault = bytes[at + 16];
        layer.hasUserMask = true;
        at = maskEnd;
    }

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
        const auto keySpan = bytes.subspan(at + 4, 4);
        const bool longLength = version == 2 && psbEightByteAdditionalLength(keySpan);
        const std::size_t lengthBytes = longLength ? 8u : 4u;
        if (end - at < 8 + lengthBytes)
            return error(ErrorCode::CorruptData, "psd.additional-info-length");
        const auto blockLength = sizedLength(bytes, at + 8, lengthBytes);
        at += 8 + lengthBytes;
        if (blockLength > end - at)
            return error(ErrorCode::CorruptData, "psd.additional-info-bounds");
        if (std::memcmp(key, "luni", 4) == 0) {
            auto decoded = decodeUnicodeName(bytes.subspan(at, std::size_t(blockLength)));
            if (!decoded)
                return decoded.error();
            layer.name = std::move(decoded.value());
        } else if (std::memcmp(key, "TySh", 4) == 0 ||
                   std::memcmp(key, "tySh", 4) == 0 ||
                   std::memcmp(key, "SoLE", 4) == 0 ||
                   std::memcmp(key, "SoLd", 4) == 0 ||
                   std::memcmp(key, "PlLd", 4) == 0 ||
                   std::memcmp(key, "plLd", 4) == 0 ||
                   std::memcmp(key, "lnk2", 4) == 0 ||
                   std::memcmp(key, "lnkD", 4) == 0 ||
                   std::memcmp(key, "lnk3", 4) == 0 ||
                   std::memcmp(key, "lnkE", 4) == 0) {
            return Error{ErrorCode::UnsupportedFeature, "psd.unsupported-layer-feature",
                         std::string(reinterpret_cast<const char*>(key), 4)};
        } else if (std::memcmp(key, "lsct", 4) == 0 ||
                   std::memcmp(key, "lsdk", 4) == 0) {
            if (blockLength < 4)
                return error(ErrorCode::CorruptData, "psd.section-divider-length");
            const auto type = u32(bytes.data() + at);
            if (type > 3)
                return error(ErrorCode::UnsupportedFeature, "psd.section-divider-type");
            layer.sectionType = type;
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
    for (std::size_t index = 6; index < 12; ++index) {
        if (bytes[index] != 0)
            return error(ErrorCode::CorruptData, "psd.header-reserved");
    }

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

    if (!reader.take(4))
        return error(ErrorCode::CorruptData, "psd.color-mode-length");
    const auto colorModeLength = u32(bytes.data() + reader.position - 4);
    if (colorModeLength > bytes.size() - reader.position || !reader.take(colorModeLength))
        return error(ErrorCode::CorruptData, "psd.color-mode-bounds");

    if (!reader.take(4))
        return error(ErrorCode::CorruptData, "psd.image-resources-length");
    const auto resourcesLength = u32(bytes.data() + reader.position - 4);
    if (resourcesLength > bytes.size() - reader.position)
        return error(ErrorCode::CorruptData, "psd.image-resources-bounds");
    const auto resourcesBegin = reader.position;
    const auto resourcesEnd = resourcesBegin + std::size_t(resourcesLength);
    auto imageResources = readImageResources(bytes, resourcesBegin, resourcesEnd, out);
    if (!imageResources)
        return imageResources.error();
    reader.position = resourcesEnd;

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

    const auto located = locateLayerInfo(bytes, reader.position, sectionEnd, version, out.depth);
    if (!located)
        return located.error();
    if (!located.value())
        return out;
    reader.position = located.value()->begin;
    const auto layerInfoEnd = located.value()->end;

    if (!reader.take(2) || reader.position > layerInfoEnd)
        return error(ErrorCode::CorruptData, "psd.layer-count");

    const auto signedCount = std::int16_t(u16(bytes.data() + reader.position - 2));
    out.signedLayerCount = signedCount;
    const int layerCount = signedCount < 0 ? -signedCount : signedCount;
    if (std::uint64_t(layerCount) > limits.nodes)
        return error(ErrorCode::BudgetExceeded, "psd.layer-count-limit");

    for (int index = 0; index < layerCount; ++index) {
        if (reader.position + 18 > layerInfoEnd)
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
        if (channelRecords > layerInfoEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.channel-records");
        std::array<std::int16_t, 56> channelIds{};
        for (std::uint16_t channel = 0; channel < layer.channels; ++channel) {
            const auto id = std::int16_t(u16(bytes.data() + reader.position +
                                             std::size_t(channel) * descriptorSize));
            for (std::uint16_t previous = 0; previous < channel; ++previous) {
                if (channelIds[previous] == id)
                    return error(ErrorCode::CorruptData, "psd.channel-duplicate");
            }
            channelIds[channel] = id;
        }
        reader.position += std::size_t(channelRecords);

        if (reader.position + 16 > layerInfoEnd)
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
        layer.visible = (bytes[reader.position + 2] & 0x02u) == 0;
        reader.position += 4;

        const auto extra = std::uint64_t(u32(bytes.data() + reader.position));
        reader.position += 4;
        if (extra > layerInfoEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.extra");
        const auto extraBegin = reader.position;
        const auto extraEnd = extraBegin + std::size_t(extra);
        auto parsedExtra = readLayerExtra(bytes, extraBegin, extraEnd, version, layer);
        if (!parsedExtra)
            return parsedExtra.error();
        reader.position = extraEnd;
        out.layers.push_back(std::move(layer));
    }

    return out;
}

namespace {

struct ChannelRecord {
    std::int16_t id{};
    std::uint64_t length{};
};

struct LayerDecodeRecord {
    PhotoshopLayerInfo info;
    std::vector<ChannelRecord> channels;
};

Result<Bytes> decodePackBitsPlane(std::span<const std::uint8_t> payload,
                                  std::uint32_t rows,
                                  std::uint64_t rowBytes,
                                  bool psb,
                                  std::uint64_t byteLimit) {
    auto planeBytes = checkedMultiply(rows, rowBytes);
    if (!planeBytes)
        return planeBytes.error();
    if (planeBytes.value() > byteLimit || planeBytes.value() > SIZE_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.rle-plane-budget");

    const std::size_t countBytes = psb ? 4u : 2u;
    auto tableBytes = checkedMultiply(rows, countBytes);
    if (!tableBytes || tableBytes.value() > payload.size())
        return error(ErrorCode::CorruptData, "psd.rle-row-table");

    std::vector<std::uint32_t> rowLengths;
    rowLengths.reserve(rows);
    std::size_t position = 0;
    for (std::uint32_t row = 0; row < rows; ++row) {
        const auto length = psb ? u32(payload.data() + position)
                                : std::uint32_t(u16(payload.data() + position));
        rowLengths.push_back(length);
        position += countBytes;
    }

    Bytes plane(std::size_t(planeBytes.value()), 0);
    std::size_t output = 0;
    for (std::uint32_t row = 0; row < rows; ++row) {
        if (rowLengths[row] > payload.size() - position)
            return error(ErrorCode::CorruptData, "psd.rle-row-bounds");
        const auto rowEnd = position + std::size_t(rowLengths[row]);
        const auto outputEnd = output + std::size_t(rowBytes);
        if (outputEnd > plane.size())
            return error(ErrorCode::CorruptData, "psd.rle-output-bounds");

        while (position < rowEnd && output < outputEnd) {
            const auto control = static_cast<std::int8_t>(payload[position++]);
            if (control >= 0) {
                const auto count = std::size_t(std::uint8_t(control)) + 1u;
                if (count > rowEnd - position || count > outputEnd - output)
                    return error(ErrorCode::CorruptData, "psd.rle-literal");
                std::copy_n(payload.begin() + std::ptrdiff_t(position),
                            std::ptrdiff_t(count),
                            plane.begin() + std::ptrdiff_t(output));
                position += count;
                output += count;
            } else if (control >= -127) {
                const auto count = std::size_t(1 - int(control));
                if (position >= rowEnd || count > outputEnd - output)
                    return error(ErrorCode::CorruptData, "psd.rle-repeat");
                const auto value = payload[position++];
                std::fill_n(plane.begin() + std::ptrdiff_t(output),
                            std::ptrdiff_t(count), value);
                output += count;
            }
        }
        if (position != rowEnd || output != outputEnd)
            return error(ErrorCode::CorruptData, "psd.rle-row-size");
    }
    return plane;
}

Result<Bytes> decodeZipPlane(std::span<const std::uint8_t> payload,
                             std::uint64_t expectedBytes,
                             std::uint64_t byteLimit) {
    if (expectedBytes > byteLimit || expectedBytes > SIZE_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.zip-plane-budget");
#ifdef DRAWBORAD_HAS_ZLIB
    if (expectedBytes > std::uint64_t(std::numeric_limits<uLongf>::max()) ||
        payload.size() > std::size_t(std::numeric_limits<uLong>::max()))
        return error(ErrorCode::BudgetExceeded, "psd.zip-zlib-limit");
    Bytes out(std::size_t(expectedBytes), 0);
    uLongf produced = uLongf(expectedBytes);
    const auto status = ::uncompress(out.data(), &produced, payload.data(), uLong(payload.size()));
    if (status != Z_OK || produced != expectedBytes)
        return error(ErrorCode::CorruptData, "psd.zip-data");
    return out;
#else
    (void)payload;
    return error(ErrorCode::UnsupportedFeature, "psd.zip-zlib-unavailable");
#endif
}

Result<void> undoZipPrediction8(Bytes& plane, std::uint64_t width, std::uint64_t height) {
    auto expected = checkedMultiply(width, height);
    if (!expected)
        return expected.error();
    if (expected.value() != plane.size())
        return error(ErrorCode::CorruptData, "psd.zip-predict-size");
    for (std::uint64_t y = 0; y < height; ++y) {
        const auto row = y * width;
        for (std::uint64_t x = 1; x < width; ++x) {
            const auto at = std::size_t(row + x);
            plane[at] = std::uint8_t(std::uint32_t(plane[at]) +
                                     std::uint32_t(plane[at - 1]));
        }
    }
    return {};
}

Result<void> undoZipPrediction16(Bytes& plane, std::uint64_t width, std::uint64_t height) {
    auto samples = checkedMultiply(width, height);
    if (!samples)
        return samples.error();
    auto expected = checkedMultiply(samples.value(), 2);
    if (!expected)
        return expected.error();
    if (expected.value() != plane.size())
        return error(ErrorCode::CorruptData, "psd.zip-predict-size");
    for (std::uint64_t y = 0; y < height; ++y) {
        const auto row = y * width;
        for (std::uint64_t x = 1; x < width; ++x) {
            const auto previous = std::size_t((row + x - 1) * 2);
            const auto current = std::size_t((row + x) * 2);
            const auto prevValue = std::uint16_t((std::uint16_t(plane[previous]) << 8) |
                                                 plane[previous + 1]);
            const auto delta = std::uint16_t((std::uint16_t(plane[current]) << 8) |
                                             plane[current + 1]);
            const auto value = std::uint16_t(prevValue + delta);
            plane[current] = std::uint8_t(value >> 8);
            plane[current + 1] = std::uint8_t(value);
        }
    }
    return {};
}

std::uint64_t removeWhiteMatteSample(std::uint64_t stored,
                                     std::uint64_t alpha,
                                     std::uint64_t maximum) {
    if (alpha == 0 || alpha == maximum)
        return stored;
    const auto background = maximum - alpha;
    if (stored <= background)
        return 0;
    const auto numerator = (stored - background) * maximum;
    return std::min(maximum, (numerator + alpha / 2u) / alpha);
}

Result<PhotoshopDecoded> decodeRawLayers(std::span<const std::uint8_t> bytes, ProfileId profile,
                                        const CodecLimits& limits) {
    auto metadata = readPhotoshopHeader(bytes, limits);
    if (!metadata)
        return metadata.error();
    const std::uint16_t version =
        metadata.value().format == ImageFormat::Psd ? std::uint16_t(1) : std::uint16_t(2);
    const auto lengthBytes = version == 1 ? std::size_t(4) : std::size_t(8);

    Reader reader{bytes, 26};
    for (int section = 0; section < 2; ++section) {
        if (!reader.take(4))
            return error(ErrorCode::CorruptData, "psd.decode-section");
        const auto length = std::uint64_t(u32(bytes.data() + reader.position - 4));
        if (length > bytes.size() - reader.position || !reader.take(std::size_t(length)))
            return error(ErrorCode::CorruptData, "psd.decode-section-bounds");
    }
    if (!reader.take(lengthBytes))
        return error(ErrorCode::CorruptData, "psd.decode-layer-mask-length");
    const auto total = version == 1 ? std::uint64_t(u32(bytes.data() + reader.position - 4))
                                    : u64(bytes.data() + reader.position - 8);
    if (total > bytes.size() - reader.position)
        return error(ErrorCode::CorruptData, "psd.decode-layer-mask-bounds");
    const auto sectionEnd = reader.position + std::size_t(total);

    std::vector<LayerDecodeRecord> records;
    std::size_t layerInfoEnd = reader.position;
    if (total != 0) {
        const auto located = locateLayerInfo(bytes, reader.position, sectionEnd, version,
                                             metadata.value().depth);
        if (!located)
            return located.error();
        if (located.value()) {
            reader.position = located.value()->begin;
            layerInfoEnd = located.value()->end;

            if (!reader.take(2) || reader.position > layerInfoEnd)
                return error(ErrorCode::CorruptData, "psd.decode-layer-count");
            const auto signedCount = std::int16_t(u16(bytes.data() + reader.position - 2));
            const int layerCount = signedCount < 0 ? -signedCount : signedCount;
            if (std::uint64_t(layerCount) > limits.nodes)
                return error(ErrorCode::BudgetExceeded, "psd.decode-layer-count-limit");

            records.reserve(std::size_t(layerCount));
            for (int index = 0; index < layerCount; ++index) {
        if (reader.position + 18 > layerInfoEnd)
            return error(ErrorCode::CorruptData, "psd.decode-layer-record");
        LayerDecodeRecord record;
        record.info.top = std::int32_t(u32(bytes.data() + reader.position));
        record.info.left = std::int32_t(u32(bytes.data() + reader.position + 4));
        record.info.bottom = std::int32_t(u32(bytes.data() + reader.position + 8));
        record.info.right = std::int32_t(u32(bytes.data() + reader.position + 12));
        reader.position += 16;
        record.info.channels = u16(bytes.data() + reader.position);
        reader.position += 2;
        if (record.info.channels > 56)
            return error(ErrorCode::CorruptData, "psd.decode-channel-count");
        record.channels.reserve(record.info.channels);
        for (std::uint16_t channel = 0; channel < record.info.channels; ++channel) {
            if (reader.position + 2 + lengthBytes > layerInfoEnd)
                return error(ErrorCode::CorruptData, "psd.decode-channel-record");
            const auto id = std::int16_t(u16(bytes.data() + reader.position));
            reader.position += 2;
            const auto length =
                version == 1 ? std::uint64_t(u32(bytes.data() + reader.position))
                             : u64(bytes.data() + reader.position);
            reader.position += lengthBytes;
            if (length < 2)
                return error(ErrorCode::CorruptData, "psd.decode-channel-length");
            if (std::any_of(record.channels.begin(), record.channels.end(),
                            [id](const ChannelRecord& existing) {
                                return existing.id == id;
                            }))
                return error(ErrorCode::CorruptData, "psd.channel-duplicate");
            record.channels.push_back({id, length});
        }
        if (reader.position + 12 > layerInfoEnd)
            return error(ErrorCode::CorruptData, "psd.decode-layer-fixed");
        const auto* key = bytes.data() + reader.position + 4;
        if (std::memcmp(bytes.data() + reader.position, "8BIM", 4) != 0)
            return error(ErrorCode::CorruptData, "psd.decode-blend-signature");
        if (std::memcmp(key, "norm", 4) == 0) record.info.blend = BlendMode::Normal;
        else if (std::memcmp(key, "mul ", 4) == 0) record.info.blend = BlendMode::Multiply;
        else if (std::memcmp(key, "scrn", 4) == 0) record.info.blend = BlendMode::Screen;
        else if (std::memcmp(key, "dark", 4) == 0) record.info.blend = BlendMode::Darken;
        else if (std::memcmp(key, "lite", 4) == 0) record.info.blend = BlendMode::Lighten;
        else return error(ErrorCode::UnsupportedFeature, "psd.decode-blend-mode");
        reader.position += 8;
        record.info.opacity = bytes[reader.position];
        record.info.visible = (bytes[reader.position + 2] & 0x02u) == 0;
        reader.position += 4;
        if (reader.position + 4 > layerInfoEnd)
            return error(ErrorCode::CorruptData, "psd.decode-extra-length");
        const auto extra = std::uint64_t(u32(bytes.data() + reader.position));
        reader.position += 4;
        if (extra > layerInfoEnd - reader.position)
            return error(ErrorCode::CorruptData, "psd.decode-extra-bounds");
        const auto extraBegin = reader.position;
        const auto extraEnd = extraBegin + std::size_t(extra);
        auto parsedExtra = readLayerExtra(bytes, extraBegin, extraEnd, version, record.info);
        if (!parsedExtra)
            return parsedExtra.error();
        reader.position = extraEnd;
                records.push_back(std::move(record));
            }
        }
    }

    const auto baseChannels = metadata.value().colorMode == 3 ? 3u :
                              metadata.value().colorMode == 4 ? 4u : 0u;
    if (!baseChannels)
        return error(ErrorCode::UnsupportedFeature, "psd.decode-color-mode");
    const auto depth = metadata.value().depth == 8 ? Depth::U8 : Depth::U16;
    const auto sampleBytes = depth == Depth::U8 ? 1u : 2u;

    PhotoshopDecoded decoded;
    decoded.info = metadata.value();
    decoded.layers.reserve(records.size());

    for (const auto& record : records) {
        if (record.info.right < record.info.left || record.info.bottom < record.info.top)
            return error(ErrorCode::CorruptData, "psd.decode-layer-rectangle");
        const auto width = std::uint64_t(record.info.right - record.info.left);
        const auto height = std::uint64_t(record.info.bottom - record.info.top);
        const bool hasAlpha = std::any_of(record.channels.begin(), record.channels.end(),
                                          [](const ChannelRecord& c) { return c.id == -1; });
        PixelFormat format{baseChannels == 3 ? ColorMode::RGB : ColorMode::CMYK, depth, hasAlpha};
        const auto outputChannels = format.channelCount();
        auto pixels = checkedMultiply(width, height);
        if (!pixels)
            return pixels.error();
        auto samples = checkedMultiply(pixels.value(), outputChannels);
        if (!samples)
            return samples.error();
        auto byteCount = checkedMultiply(samples.value(), sampleBytes);
        if (!byteCount || byteCount.value() > limits.decodedBytes ||
            byteCount.value() > SIZE_MAX)
            return error(ErrorCode::BudgetExceeded, "psd.decode-layer-bytes");

        TileData tile;
        tile.format = format;
        tile.width = std::uint32_t(width);
        tile.height = std::uint32_t(height);
        tile.rowStride = width * outputChannels * sampleBytes;
        tile.profileId = profile;
        tile.bytes.assign(std::size_t(byteCount.value()), 0);

        Bytes userMask;
        for (const auto& channel : record.channels) {
            if (channel.length > layerInfoEnd - reader.position)
                return error(ErrorCode::CorruptData, "psd.decode-channel-bounds");
            const auto channelEnd = reader.position + std::size_t(channel.length);
            if (reader.position + 2 > channelEnd)
                return error(ErrorCode::CorruptData, "psd.decode-channel-compression");
            const auto compression = u16(bytes.data() + reader.position);
            reader.position += 2;
            const bool maskChannel = channel.id == -2;
            const int destination = channel.id >= 0 && channel.id < int(baseChannels)
                                        ? int(channel.id)
                                        : channel.id == -1 ? int(baseChannels) : -1;
            const auto channelWidth = maskChannel ? record.info.maskBounds.width : width;
            const auto channelHeight = maskChannel ? record.info.maskBounds.height : height;
            if (maskChannel && (!record.info.hasUserMask || !channelWidth || !channelHeight))
                return error(ErrorCode::CorruptData, "psd.decode-mask-metadata");
            auto channelPixels = checkedMultiply(channelWidth, channelHeight);
            if (!channelPixels)
                return channelPixels.error();
            auto planeBytes = checkedMultiply(channelPixels.value(), sampleBytes);
            if (!planeBytes)
                return planeBytes.error();

            Bytes decodedPlane;
            std::span<const std::uint8_t> plane;
            if (compression == 0) {
                if (planeBytes.value() > channelEnd - reader.position)
                    return error(ErrorCode::CorruptData, "psd.decode-raw-size");
                plane = bytes.subspan(reader.position, std::size_t(planeBytes.value()));
            } else if (compression == 1) {
                auto rle = decodePackBitsPlane(
                    bytes.subspan(reader.position, channelEnd - reader.position),
                    std::uint32_t(channelHeight), channelWidth * sampleBytes,
                    version == 2, limits.decodedBytes);
                if (!rle)
                    return rle.error();
                decodedPlane = std::move(rle.value());
                plane = decodedPlane;
            } else if (compression == 2 || compression == 3) {
                auto zip = decodeZipPlane(
                    bytes.subspan(reader.position, channelEnd - reader.position),
                    planeBytes.value(), limits.decodedBytes);
                if (!zip)
                    return zip.error();
                decodedPlane = std::move(zip.value());
                if (compression == 3) {
                    Result<void> prediction =
                        sampleBytes == 1
                            ? undoZipPrediction8(decodedPlane, channelWidth, channelHeight)
                            : undoZipPrediction16(decodedPlane, channelWidth, channelHeight);
                    if (!prediction)
                        return prediction.error();
                }
                plane = decodedPlane;
            } else {
                reader.position = channelEnd;
                return error(ErrorCode::UnsupportedFeature, "psd.decode-compression");
            }

            if (maskChannel) {
                userMask.resize(std::size_t(channelPixels.value()));
                for (std::uint64_t pixel = 0; pixel < channelPixels.value(); ++pixel) {
                    const auto src = std::size_t(pixel * sampleBytes);
                    userMask[std::size_t(pixel)] =
                        sampleBytes == 1 ? plane[src] : plane[src];
                }
            } else if (destination >= 0) {
                for (std::uint64_t pixel = 0; pixel < pixels.value(); ++pixel) {
                    const auto src = std::size_t(pixel * sampleBytes);
                    const auto dst = std::size_t(pixel * outputChannels * sampleBytes +
                                                 std::uint64_t(destination) * sampleBytes);
                    if (sampleBytes == 1) {
                        auto value = plane[src];
                        if (format.mode == ColorMode::CMYK &&
                            destination < int(baseChannels))
                            value = std::uint8_t(255u - value);
                        tile.bytes[dst] = value;
                    } else {
                        auto value = std::uint16_t(
                            (std::uint16_t(plane[src]) << 8) | plane[src + 1]);
                        if (format.mode == ColorMode::CMYK &&
                            destination < int(baseChannels))
                            value = std::uint16_t(65535u - value);
                        tile.bytes[dst] = std::uint8_t(value);
                        tile.bytes[dst + 1] = std::uint8_t(value >> 8);
                    }
                }
            }
            reader.position = channelEnd;
        }
        decoded.layers.push_back({record.info, std::move(tile), std::move(userMask)});
    }

    reader.position = sectionEnd;
    if (reader.position == bytes.size())
        return decoded;
    if (reader.position + 2 > bytes.size())
        return error(ErrorCode::CorruptData, "psd.composite-compression");
    const auto compositeCompression = u16(bytes.data() + reader.position);
    reader.position += 2;
    if (reader.position == bytes.size())
        return decoded;

    const bool hasExtraChannel = metadata.value().channels == baseChannels + 1u;
    if (metadata.value().channels != baseChannels && !hasExtraChannel)
        return error(ErrorCode::UnsupportedFeature, "psd.composite-extra-channels");
    if (hasExtraChannel && metadata.value().signedLayerCount &&
        *metadata.value().signedLayerCount > 0)
        return error(ErrorCode::UnsupportedFeature, "psd.composite-extra-channels");
    const bool compositeAlpha = hasExtraChannel;
    PixelFormat compositeFormat{
        baseChannels == 3 ? ColorMode::RGB : ColorMode::CMYK,
        depth,
        compositeAlpha
    };
    const auto compositeChannels = compositeFormat.channelCount();
    auto compositePixels =
        checkedMultiply(metadata.value().size.width, metadata.value().size.height);
    if (!compositePixels)
        return compositePixels.error();
    auto compositeSamples = checkedMultiply(compositePixels.value(), compositeChannels);
    if (!compositeSamples)
        return compositeSamples.error();
    auto compositeByteCount = checkedMultiply(compositeSamples.value(), sampleBytes);
    if (!compositeByteCount || compositeByteCount.value() > limits.decodedBytes ||
        compositeByteCount.value() > SIZE_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.composite-budget");

    Bytes decodedComposite;
    std::span<const std::uint8_t> compositePlanes;
    if (compositeCompression == 0) {
        if (compositeByteCount.value() > bytes.size() - reader.position)
            return error(ErrorCode::CorruptData, "psd.composite-bounds");
        compositePlanes =
            bytes.subspan(reader.position, std::size_t(compositeByteCount.value()));
    } else if (compositeCompression == 1) {
        auto rows = checkedMultiply(metadata.value().size.height, compositeChannels);
        if (!rows || rows.value() > std::numeric_limits<std::uint32_t>::max())
            return error(ErrorCode::BudgetExceeded, "psd.composite-rle-rows");
        auto rowBytes = checkedMultiply(metadata.value().size.width, sampleBytes);
        if (!rowBytes)
            return rowBytes.error();
        auto rle = decodePackBitsPlane(
            bytes.subspan(reader.position),
            std::uint32_t(rows.value()), rowBytes.value(),
            version == 2, limits.decodedBytes);
        if (!rle)
            return rle.error();
        decodedComposite = std::move(rle.value());
        compositePlanes = decodedComposite;
    } else if (compositeCompression == 2 || compositeCompression == 3) {
        auto zip = decodeZipPlane(bytes.subspan(reader.position),
                                  compositeByteCount.value(), limits.decodedBytes);
        if (!zip)
            return zip.error();
        decodedComposite = std::move(zip.value());
        if (compositeCompression == 3) {
            auto predictionRows =
                checkedMultiply(metadata.value().size.height, compositeChannels);
            if (!predictionRows)
                return predictionRows.error();
            Result<void> prediction =
                sampleBytes == 1
                    ? undoZipPrediction8(decodedComposite, metadata.value().size.width,
                                         predictionRows.value())
                    : undoZipPrediction16(decodedComposite, metadata.value().size.width,
                                          predictionRows.value());
            if (!prediction)
                return prediction.error();
        }
        compositePlanes = decodedComposite;
    } else {
        return error(ErrorCode::UnsupportedFeature, "psd.composite-compression");
    }

    TileData composite;
    composite.width = std::uint32_t(metadata.value().size.width);
    composite.height = std::uint32_t(metadata.value().size.height);
    composite.format = compositeFormat;
    composite.profileId = profile;
    composite.rowStride =
        metadata.value().size.width * compositeChannels * sampleBytes;
    composite.bytes.assign(std::size_t(compositeByteCount.value()), 0);

    for (unsigned channel = 0; channel < compositeChannels; ++channel) {
        for (std::uint64_t pixel = 0; pixel < compositePixels.value(); ++pixel) {
            const auto src =
                std::size_t((std::uint64_t(channel) * compositePixels.value() +
                             pixel) * sampleBytes);
            const auto dst = std::size_t(pixel * compositeChannels * sampleBytes +
                                         std::uint64_t(channel) * sampleBytes);
            if (sampleBytes == 1) {
                auto value = compositePlanes[src];
                if (compositeFormat.mode == ColorMode::CMYK && channel < baseChannels)
                    value = std::uint8_t(255u - value);
                composite.bytes[dst] = value;
            } else {
                auto value = std::uint16_t(
                    (std::uint16_t(compositePlanes[src]) << 8) | compositePlanes[src + 1]);
                if (compositeFormat.mode == ColorMode::CMYK && channel < baseChannels)
                    value = std::uint16_t(65535u - value);
                composite.bytes[dst] = std::uint8_t(value);
                composite.bytes[dst + 1] = std::uint8_t(value >> 8);
            }
        }
    }
    if (compositeFormat.mode == ColorMode::RGB && compositeAlpha) {
        for (std::uint64_t pixel = 0; pixel < compositePixels.value(); ++pixel) {
            const auto base = std::size_t(
                pixel * compositeChannels * sampleBytes);
            if (sampleBytes == 1) {
                const auto alpha = composite.bytes[base + baseChannels];
                for (unsigned channel = 0; channel < baseChannels; ++channel) {
                    composite.bytes[base + channel] = std::uint8_t(
                        removeWhiteMatteSample(
                            composite.bytes[base + channel], alpha, 255u));
                }
            } else {
                const auto alphaAt = base + std::size_t(baseChannels) * 2u;
                const auto alpha = std::uint16_t(
                    composite.bytes[alphaAt] |
                    (std::uint16_t(composite.bytes[alphaAt + 1]) << 8));
                for (unsigned channel = 0; channel < baseChannels; ++channel) {
                    const auto at = base + std::size_t(channel) * 2u;
                    const auto value = std::uint16_t(
                        composite.bytes[at] |
                        (std::uint16_t(composite.bytes[at + 1]) << 8));
                    const auto straight = std::uint16_t(
                        removeWhiteMatteSample(value, alpha, 65535u));
                    composite.bytes[at] = std::uint8_t(straight);
                    composite.bytes[at + 1] = std::uint8_t(straight >> 8);
                }
            }
        }
    }

    decoded.composite = std::move(composite);
    return decoded;
}

} // namespace

Result<PhotoshopDecoded> decodePhotoshopLayers(std::span<const std::uint8_t> bytes,
                                               ProfileId profile,
                                               const CodecLimits& limits) {
    return decodeRawLayers(bytes, profile, limits);
}

Result<ImportResult> readPhotoshop(const InputPath& path,const ImportPolicy& policy,
                                   std::stop_token stop){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"psd.cancelled");
    if(path.empty())return error(ErrorCode::InvalidInput,"psd.input-path");
    NativeFileIO io;
    auto bytes=io.read(path,policy.limits.encodedBytes);
    if(!bytes)return bytes.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"psd.cancelled");
    auto decoded=decodePhotoshopLayers(bytes.value(),policy.profile,policy.limits);
    if(!decoded)return decoded.error();
    return ImportResult{std::move(decoded.value()),SourceKind::ImportedRaster};
}

} // namespace drawborad
