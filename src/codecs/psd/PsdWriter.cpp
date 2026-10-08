#include "PsdWriter.h"
#include "storage/FileIO.h"
#include "codecs/AtomicExport.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace drawborad {
namespace {

void put16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(std::uint8_t(value >> 8));
    bytes.push_back(std::uint8_t(value));
}

void put32(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(std::uint8_t(value >> 24));
    bytes.push_back(std::uint8_t(value >> 16));
    bytes.push_back(std::uint8_t(value >> 8));
    bytes.push_back(std::uint8_t(value));
}

void put32s(Bytes& bytes, std::int32_t value) {
    put32(bytes, std::uint32_t(value));
}

std::uint16_t encodedLayerCount(std::size_t count, bool mergedTransparency) {
    auto signedCount = std::int16_t(count);
    if (mergedTransparency && signedCount > 0)
        signedCount = std::int16_t(-signedCount);
    return std::uint16_t(signedCount);
}

Bytes mergedTransparencyBlock(Depth depth) {
    Bytes block{'8','B','I','M'};
    const char* key = depth == Depth::U16 ? "Mt16" : "Mtrn";
    block.insert(block.end(), key, key + 4);
    put32(block, 0);
    return block;
}

bool psdRectFitsInt32(const Rect& rect) {
    if (!rect.width || !rect.height ||
        rect.x < std::numeric_limits<std::int32_t>::min() ||
        rect.y < std::numeric_limits<std::int32_t>::min() ||
        rect.x > std::numeric_limits<std::int32_t>::max() ||
        rect.y > std::numeric_limits<std::int32_t>::max())
        return false;
    const auto maxXSpan = std::uint64_t(
        std::int64_t(std::numeric_limits<std::int32_t>::max()) - rect.x);
    const auto maxYSpan = std::uint64_t(
        std::int64_t(std::numeric_limits<std::int32_t>::max()) - rect.y);
    return rect.width <= maxXSpan && rect.height <= maxYSpan;
}

std::uint64_t whiteMatteSample(std::uint64_t value, std::uint64_t alpha,
                               std::uint64_t maximum) {
    if (alpha == 0 || alpha == maximum)
        return value;
    const auto numerator = value * alpha + maximum * (maximum - alpha);
    return (numerator + maximum / 2u) / maximum;
}

void appendImageResource(Bytes& resources, std::uint16_t id,
                         std::span<const std::uint8_t> data) {
    resources.insert(resources.end(), {'8','B','I','M'});
    put16(resources, id);
    resources.push_back(0);
    resources.push_back(0);
    put32(resources, std::uint32_t(data.size()));
    resources.insert(resources.end(), data.begin(), data.end());
    if ((data.size() & 1u) != 0)
        resources.push_back(0);
}

Result<Bytes> imageResources(const PsdWriteRequest& request) {
    if (!std::isfinite(request.ppi) || request.ppi <= 0.0 || request.ppi > 100000.0)
        return error(ErrorCode::InvalidInput, "psd.ppi");
    Bytes resources;
    Bytes resolution;
    const auto fixed = std::uint32_t(std::llround(request.ppi * 65536.0));
    put32(resolution, fixed);
    put16(resolution, 1);
    put16(resolution, 1);
    put32(resolution, fixed);
    put16(resolution, 1);
    put16(resolution, 1);
    appendImageResource(resources, 1005, resolution);
    if (!request.iccProfile.empty()) {
        if (request.iccProfile.size() > UINT32_MAX)
            return error(ErrorCode::BudgetExceeded, "psd.icc-size");
        appendImageResource(resources, 1039, request.iccProfile);
    }
    return resources;
}

Result<std::vector<std::uint16_t>> utf16Units(std::string_view text) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = std::uint8_t(text[i]);
        std::uint32_t cp = 0;
        std::size_t width = 0;
        if (first <= 0x7f) {
            cp = first;
            width = 1;
        } else if ((first & 0xe0u) == 0xc0u) {
            cp = first & 0x1fu;
            width = 2;
        } else if ((first & 0xf0u) == 0xe0u) {
            cp = first & 0x0fu;
            width = 3;
        } else if ((first & 0xf8u) == 0xf0u) {
            cp = first & 0x07u;
            width = 4;
        } else {
            return error(ErrorCode::InvalidInput, "psd.layer-name-utf8");
        }
        if (i + width > text.size())
            return error(ErrorCode::InvalidInput, "psd.layer-name-utf8");
        for (std::size_t j = 1; j < width; ++j) {
            const auto next = std::uint8_t(text[i + j]);
            if ((next & 0xc0u) != 0x80u)
                return error(ErrorCode::InvalidInput, "psd.layer-name-utf8");
            cp = (cp << 6) | (next & 0x3fu);
        }
        const std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < minimum[width] || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return error(ErrorCode::InvalidInput, "psd.layer-name-utf8");
        if (cp <= 0xffff) {
            out.push_back(std::uint16_t(cp));
        } else {
            cp -= 0x10000;
            out.push_back(std::uint16_t(0xd800u + (cp >> 10)));
            out.push_back(std::uint16_t(0xdc00u + (cp & 0x3ffu)));
        }
        i += width;
    }
    if (out.size() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.layer-name-length");
    return out;
}

Result<void> appendUnicodeName(Bytes& extra, std::string_view name) {
    auto units = utf16Units(name);
    if (!units)
        return units.error();
    Bytes data;
    put32(data, std::uint32_t(units.value().size()));
    for (const auto unit : units.value())
        put16(data, unit);
    extra.insert(extra.end(), {'8', 'B', 'I', 'M', 'l', 'u', 'n', 'i'});
    put32(extra, std::uint32_t(data.size()));
    extra.insert(extra.end(), data.begin(), data.end());
    if ((data.size() & 1u) != 0)
        extra.push_back(0);
    return {};
}

} // namespace

Result<void> preflightPsdOutputSize(const PsdWriteRequest& request) {
    constexpr std::uint64_t maxPsdBytes = 2 * GiB;
    const auto baseChannels = request.format.mode == ColorMode::RGB ? 3u : 4u;
    const auto channelCount = baseChannels + (request.format.alpha ? 1u : 0u);
    const auto sampleBytes = request.format.depth == Depth::U16 ? 2u : 1u;
    auto pixels = checkedMultiply(request.size.width, request.size.height);
    if (!pixels) return pixels.error();
    auto plane = checkedMultiply(pixels.value(), sampleBytes);
    if (!plane) return plane.error();
    auto fullChannels = checkedMultiply(plane.value(), channelCount);
    if (!fullChannels) return fullChannels.error();

    // PSD stores every layer channel and the merged composite separately.
    // Count the raw payload before validating backing buffers so impossible
    // >2 GiB documents fail without traversing or allocating pixel data.
    std::uint64_t minimum = 64;
    for (const auto& layer : request.layers) {
        auto next = checkedAdd(minimum, fullChannels.value());
        if (!next) return next.error();
        minimum = next.value();
        if (!layer.mask.empty()) {
            auto maskPixels = checkedMultiply(layer.maskBounds.width, layer.maskBounds.height);
            if (!maskPixels) return maskPixels.error();
            auto maskBytes = checkedMultiply(maskPixels.value(), sampleBytes);
            if (!maskBytes) return maskBytes.error();
            next = checkedAdd(minimum, maskBytes.value());
            if (!next) return next.error();
            minimum = next.value();
        }
        if (minimum > maxPsdBytes)
            return error(ErrorCode::BudgetExceeded, "psd.output-size-limit");
    }
    auto merged = checkedAdd(minimum, fullChannels.value());
    if (!merged) return merged.error();
    if (merged.value() > maxPsdBytes)
        return error(ErrorCode::BudgetExceeded, "psd.output-size-limit");
    return {};
}

Result<Bytes> encodePsd(const PsdWriteRequest& request) {
    if (!request.size.width || !request.size.height || request.size.width > 30000 ||
        request.size.height > 30000)
        return error(ErrorCode::UnsupportedFeature, "psd.dimension-limit");
    if (request.format.depth != Depth::U8 && request.format.depth != Depth::U16)
        return error(ErrorCode::UnsupportedFeature, "psd.depth");
    auto outputSize = preflightPsdOutputSize(request);
    if (!outputSize)
        return outputSize.error();
    if (request.layers.size() > std::size_t(INT16_MAX))
        return error(ErrorCode::BudgetExceeded, "psd.layer-count");

    const unsigned baseChannels = request.format.mode == ColorMode::RGB ? 3u : 4u;
    const unsigned channelCount = baseChannels + (request.format.alpha ? 1u : 0u);
    const unsigned sampleBytes = request.format.depth == Depth::U16 ? 2u : 1u;

    Bytes output;
    auto appendText = [&](const char* text, std::size_t length) {
        output.insert(output.end(), text, text + length);
    };

    appendText("8BPS", 4);
    put16(output, 1);
    output.insert(output.end(), 6, 0);
    put16(output, std::uint16_t(channelCount));
    put32(output, std::uint32_t(request.size.height));
    put32(output, std::uint32_t(request.size.width));
    put16(output, std::uint16_t(request.format.depth));
    put16(output, request.format.mode == ColorMode::RGB ? 3 : 4);
    put32(output, 0);
    auto resources = imageResources(request);
    if (!resources)
        return resources.error();
    if (resources.value().size() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.image-resources-size");
    put32(output, std::uint32_t(resources.value().size()));
    output.insert(output.end(), resources.value().begin(), resources.value().end());

    Bytes records;
    put16(records, encodedLayerCount(request.layers.size(), request.format.alpha));

    for (const auto& layer : request.layers) {
        if (layer.pixels.width != request.size.width || layer.pixels.height != request.size.height ||
            layer.pixels.format != request.format)
            return error(ErrorCode::InvalidInput, "psd.layer-layout");

        const auto expectedRow =
            request.size.width * std::uint64_t(channelCount) * std::uint64_t(sampleBytes);
        if (layer.pixels.rowStride < expectedRow)
            return error(ErrorCode::CorruptData, "psd.layer-stride");

        const bool hasMask = !layer.mask.empty();
        if (hasMask) {
            if (layer.maskDefault != 0 && layer.maskDefault != 255)
                return error(ErrorCode::InvalidInput, "psd.mask-default");
            if (!psdRectFitsInt32(layer.maskBounds))
                return error(ErrorCode::InvalidInput, "psd.mask-bounds");
            auto maskPixels = checkedMultiply(layer.maskBounds.width, layer.maskBounds.height);
            if (!maskPixels || maskPixels.value() != layer.mask.size())
                return error(ErrorCode::InvalidInput, "psd.mask-layout");
        }

        put32s(records, 0);
        put32s(records, 0);
        put32(records, std::uint32_t(request.size.height));
        put32(records, std::uint32_t(request.size.width));
        const auto layerChannelCount = channelCount + (hasMask ? 1u : 0u);
        put16(records, std::uint16_t(layerChannelCount));

        const auto pixelCount = checkedMultiply(request.size.width, request.size.height);
        if (!pixelCount)
            return pixelCount.error();
        const auto channelPayload = checkedMultiply(pixelCount.value(), sampleBytes);
        if (!channelPayload)
            return channelPayload.error();
        const auto channelBytes = checkedAdd(channelPayload.value(), 2);
        if (!channelBytes || channelBytes.value() > UINT32_MAX)
            return error(ErrorCode::BudgetExceeded, "psd.channel-size");

        for (unsigned channel = 0; channel < channelCount; ++channel) {
            put16(records, channel < baseChannels ? std::uint16_t(channel) : std::uint16_t(0xffff));
            put32(records, std::uint32_t(channelBytes.value()));
        }
        if (hasMask) {
            auto maskPixels = checkedMultiply(layer.maskBounds.width, layer.maskBounds.height);
            if (!maskPixels)
                return maskPixels.error();
            auto maskPayload = checkedMultiply(maskPixels.value(), sampleBytes);
            if (!maskPayload)
                return maskPayload.error();
            auto maskBytes = checkedAdd(maskPayload.value(), 2);
            if (!maskBytes || maskBytes.value() > UINT32_MAX)
                return error(ErrorCode::BudgetExceeded, "psd.mask-channel-size");
            put16(records, std::uint16_t(0xfffe));
            put32(records, std::uint32_t(maskBytes.value()));
        }

        records.insert(records.end(), {'8', 'B', 'I', 'M'});
        const char* blendKey = "norm";
        switch (layer.blend) {
        case BlendMode::Normal: blendKey = "norm"; break;
        case BlendMode::Multiply: blendKey = "mul "; break;
        case BlendMode::Screen: blendKey = "scrn"; break;
        case BlendMode::Darken: blendKey = "dark"; break;
        case BlendMode::Lighten: blendKey = "lite"; break;
        }
        records.insert(records.end(), blendKey, blendKey + 4);
        records.push_back(layer.opacity);
        records.push_back(0);
        records.push_back(std::uint8_t(0x08u | (layer.visible ? 0u : 0x02u)));
        records.push_back(0);

        Bytes extra;
        if (hasMask) {
            put32(extra, 20);
            put32s(extra, std::int32_t(layer.maskBounds.y));
            put32s(extra, std::int32_t(layer.maskBounds.x));
            put32s(extra, std::int32_t(layer.maskBounds.y + std::int64_t(layer.maskBounds.height)));
            put32s(extra, std::int32_t(layer.maskBounds.x + std::int64_t(layer.maskBounds.width)));
            extra.push_back(layer.maskDefault);
            extra.push_back(0);
            extra.push_back(0);
            extra.push_back(0);
        } else {
            put32(extra, 0);
        }
        put32(extra, 0);
        const auto fallbackLength = std::min<std::size_t>(255, layer.name.size());
        extra.push_back(std::uint8_t(fallbackLength));
        extra.insert(extra.end(), layer.name.begin(),
                     layer.name.begin() + std::ptrdiff_t(fallbackLength));
        while (extra.size() % 4)
            extra.push_back(0);
        auto unicode = appendUnicodeName(extra, layer.name);
        if (!unicode)
            return unicode.error();
        if (layer.sectionType) {
            if (*layer.sectionType > 3)
                return error(ErrorCode::InvalidInput, "psd.section-divider-type");
            extra.insert(extra.end(), {'8', 'B', 'I', 'M', 'l', 's', 'c', 't'});
            put32(extra, 4);
            put32(extra, *layer.sectionType);
        }

        put32(records, std::uint32_t(extra.size()));
        records.insert(records.end(), extra.begin(), extra.end());
    }

    Bytes channelData;
    for (const auto& layer : request.layers) {
        for (unsigned channel = 0; channel < channelCount; ++channel) {
            put16(channelData, 0);
            for (std::uint32_t y = 0; y < layer.pixels.height; ++y) {
                for (std::uint32_t x = 0; x < layer.pixels.width; ++x) {
                    const auto offset = std::size_t(
                        std::uint64_t(y) * layer.pixels.rowStride +
                        std::uint64_t(x) * channelCount * sampleBytes +
                        std::uint64_t(channel) * sampleBytes);
                    if (offset + sampleBytes > layer.pixels.bytes.size())
                        return error(ErrorCode::CorruptData, "psd.layer-bytes");
                    if (sampleBytes == 1) {
                        auto value = layer.pixels.bytes[offset];
                        if (request.format.mode == ColorMode::CMYK && channel < baseChannels)
                            value = std::uint8_t(255u - value);
                        channelData.push_back(value);
                    } else {
                        auto value = std::uint16_t(
                            layer.pixels.bytes[offset] |
                            (std::uint16_t(layer.pixels.bytes[offset + 1]) << 8));
                        if (request.format.mode == ColorMode::CMYK && channel < baseChannels)
                            value = std::uint16_t(65535u - value);
                        channelData.push_back(std::uint8_t(value >> 8));
                        channelData.push_back(std::uint8_t(value));
                    }
                }
            }
        }
        if (!layer.mask.empty()) {
            put16(channelData, 0);
            for (auto value : layer.mask) {
                if (sampleBytes == 1) {
                    channelData.push_back(value);
                } else {
                    channelData.push_back(value);
                    channelData.push_back(value);
                }
            }
        }
    }

    Bytes layerInfo;
    layerInfo.insert(layerInfo.end(), records.begin(), records.end());
    layerInfo.insert(layerInfo.end(), channelData.begin(), channelData.end());
    if (layerInfo.size() % 2)
        layerInfo.push_back(0);

    Bytes layerMask;
    put32(layerMask, std::uint32_t(layerInfo.size()));
    layerMask.insert(layerMask.end(), layerInfo.begin(), layerInfo.end());
    put32(layerMask, 0);
    if (request.format.alpha && request.layers.empty()) {
        const auto transparency = mergedTransparencyBlock(request.format.depth);
        layerMask.insert(layerMask.end(), transparency.begin(), transparency.end());
    }

    if (layerMask.size() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.layer-mask-size");
    put32(output, std::uint32_t(layerMask.size()));
    output.insert(output.end(), layerMask.begin(), layerMask.end());

    const TileData* merged = request.merged ? &*request.merged
                                            : request.layers.size() == 1 ? &request.layers.front().pixels
                                                                         : nullptr;
    if (!merged)
        return error(ErrorCode::InvalidInput, "psd.merged-required");
    if (merged->width != request.size.width || merged->height != request.size.height ||
        merged->format != request.format)
        return error(ErrorCode::InvalidInput, "psd.merged-layout");
    const auto mergedRowBytes =
        request.size.width * std::uint64_t(channelCount) * std::uint64_t(sampleBytes);
    if (merged->rowStride < mergedRowBytes)
        return error(ErrorCode::CorruptData, "psd.merged-stride");
    auto mergedTotal = checkedMultiply(merged->rowStride, merged->height);
    if (!mergedTotal || mergedTotal.value() > merged->bytes.size())
        return error(ErrorCode::CorruptData, "psd.merged-bytes");

    put16(output, 0);
    for (unsigned channel = 0; channel < channelCount; ++channel) {
        for (std::uint32_t y = 0; y < merged->height; ++y) {
            for (std::uint32_t x = 0; x < merged->width; ++x) {
                const auto offset = std::size_t(
                    std::uint64_t(y) * merged->rowStride +
                    std::uint64_t(x) * channelCount * sampleBytes +
                    std::uint64_t(channel) * sampleBytes);
                if (offset + sampleBytes > merged->bytes.size())
                    return error(ErrorCode::CorruptData, "psd.merged-pixel");
                if (sampleBytes == 1) {
                    auto value = merged->bytes[offset];
                    if (request.format.mode == ColorMode::RGB && request.format.alpha &&
                        channel < baseChannels) {
                        const auto alphaOffset = offset +
                            std::size_t(baseChannels - channel);
                        if (alphaOffset >= merged->bytes.size())
                            return error(ErrorCode::CorruptData, "psd.merged-alpha");
                        value = std::uint8_t(whiteMatteSample(
                            value, merged->bytes[alphaOffset], 255u));
                    }
                    if (request.format.mode == ColorMode::CMYK && channel < baseChannels)
                        value = std::uint8_t(255u - value);
                    output.push_back(value);
                } else {
                    auto value = std::uint16_t(
                        merged->bytes[offset] |
                        (std::uint16_t(merged->bytes[offset + 1]) << 8));
                    if (request.format.mode == ColorMode::RGB && request.format.alpha &&
                        channel < baseChannels) {
                        const auto alphaOffset = offset +
                            std::size_t(baseChannels - channel) * 2u;
                        if (alphaOffset + 1 >= merged->bytes.size())
                            return error(ErrorCode::CorruptData, "psd.merged-alpha");
                        const auto alpha = std::uint16_t(
                            merged->bytes[alphaOffset] |
                            (std::uint16_t(merged->bytes[alphaOffset + 1]) << 8));
                        value = std::uint16_t(whiteMatteSample(value, alpha, 65535u));
                    }
                    if (request.format.mode == ColorMode::CMYK && channel < baseChannels)
                        value = std::uint16_t(65535u - value);
                    output.push_back(std::uint8_t(value >> 8));
                    output.push_back(std::uint8_t(value));
                }
            }
        }
    }

    return output;
}

Result<void> writePsd(const PsdWriteRequest& request, std::stop_token stop,
                          PsdWriteStats* stats) {
    if (stats)
        *stats = {};
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "psd.cancelled");
    if (request.path.empty())
        return error(ErrorCode::InvalidInput, "psd.output-path");
    if (!request.size.width || !request.size.height || request.size.width > 30000 ||
        request.size.height > 30000)
        return error(ErrorCode::UnsupportedFeature, "psd.dimension-limit");
    if (request.format.depth != Depth::U8 && request.format.depth != Depth::U16)
        return error(ErrorCode::UnsupportedFeature, "psd.depth");
    if (request.layers.size() > std::size_t(INT16_MAX))
        return error(ErrorCode::BudgetExceeded, "psd.layer-count");
    auto preflight = preflightPsdOutputSize(request);
    if (!preflight)
        return preflight.error();

    const unsigned baseChannels = request.format.mode == ColorMode::RGB ? 3u : 4u;
    const unsigned channelCount = baseChannels + (request.format.alpha ? 1u : 0u);
    const unsigned sampleBytes = request.format.depth == Depth::U16 ? 2u : 1u;
    auto pixelCount = checkedMultiply(request.size.width, request.size.height);
    if (!pixelCount)
        return pixelCount.error();
    auto planeBytes = checkedMultiply(pixelCount.value(), sampleBytes);
    if (!planeBytes)
        return planeBytes.error();
    auto storedChannelBytes = checkedAdd(planeBytes.value(), 2);
    if (!storedChannelBytes || storedChannelBytes.value() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.channel-size");

    auto resources = imageResources(request);
    if (!resources)
        return resources.error();
    if (resources.value().size() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.image-resources-size");

    const TileData* merged = request.merged ? &*request.merged
                                            : request.layers.size() == 1
                                                  ? &request.layers.front().pixels
                                                  : nullptr;
    if (!merged)
        return error(ErrorCode::InvalidInput, "psd.merged-required");
    if (merged->width != request.size.width || merged->height != request.size.height ||
        merged->format != request.format)
        return error(ErrorCode::InvalidInput, "psd.merged-layout");
    const auto expectedRow =
        request.size.width * std::uint64_t(channelCount) * std::uint64_t(sampleBytes);
    if (merged->rowStride < expectedRow)
        return error(ErrorCode::CorruptData, "psd.merged-stride");
    auto mergedTotal = checkedMultiply(merged->rowStride, merged->height);
    if (!mergedTotal || mergedTotal.value() > merged->bytes.size())
        return error(ErrorCode::CorruptData, "psd.merged-bytes");

    Bytes records;
    put16(records, encodedLayerCount(request.layers.size(), request.format.alpha));
    std::uint64_t channelDataBytes = 0;

    for (const auto& layer : request.layers) {
        if (layer.pixels.width != request.size.width ||
            layer.pixels.height != request.size.height ||
            layer.pixels.format != request.format)
            return error(ErrorCode::InvalidInput, "psd.layer-layout");
        if (layer.pixels.rowStride < expectedRow)
            return error(ErrorCode::CorruptData, "psd.layer-stride");
        auto layerTotal = checkedMultiply(layer.pixels.rowStride, layer.pixels.height);
        if (!layerTotal || layerTotal.value() > layer.pixels.bytes.size())
            return error(ErrorCode::CorruptData, "psd.layer-bytes");

        const bool hasMask = !layer.mask.empty();
        std::uint64_t maskPayloadBytes = 0;
        if (hasMask) {
            if (layer.maskDefault != 0 && layer.maskDefault != 255)
                return error(ErrorCode::InvalidInput, "psd.mask-default");
            if (!psdRectFitsInt32(layer.maskBounds))
                return error(ErrorCode::InvalidInput, "psd.mask-bounds");
            auto maskPixels =
                checkedMultiply(layer.maskBounds.width, layer.maskBounds.height);
            if (!maskPixels || maskPixels.value() != layer.mask.size())
                return error(ErrorCode::InvalidInput, "psd.mask-layout");
            auto payload = checkedMultiply(maskPixels.value(), sampleBytes);
            if (!payload)
                return payload.error();
            maskPayloadBytes = payload.value();
        }

        put32s(records, 0);
        put32s(records, 0);
        put32(records, std::uint32_t(request.size.height));
        put32(records, std::uint32_t(request.size.width));
        put16(records, std::uint16_t(channelCount + (hasMask ? 1u : 0u)));

        for (unsigned channel = 0; channel < channelCount; ++channel) {
            put16(records, channel < baseChannels ? std::uint16_t(channel)
                                                  : std::uint16_t(0xffff));
            put32(records, std::uint32_t(storedChannelBytes.value()));
            auto next = checkedAdd(channelDataBytes, storedChannelBytes.value());
            if (!next)
                return next.error();
            channelDataBytes = next.value();
        }
        if (hasMask) {
            auto maskStored = checkedAdd(maskPayloadBytes, 2);
            if (!maskStored || maskStored.value() > UINT32_MAX)
                return error(ErrorCode::BudgetExceeded, "psd.mask-channel-size");
            put16(records, std::uint16_t(0xfffe));
            put32(records, std::uint32_t(maskStored.value()));
            auto next = checkedAdd(channelDataBytes, maskStored.value());
            if (!next)
                return next.error();
            channelDataBytes = next.value();
        }

        records.insert(records.end(), {'8', 'B', 'I', 'M'});
        const char* blendKey = "norm";
        switch (layer.blend) {
        case BlendMode::Normal: blendKey = "norm"; break;
        case BlendMode::Multiply: blendKey = "mul "; break;
        case BlendMode::Screen: blendKey = "scrn"; break;
        case BlendMode::Darken: blendKey = "dark"; break;
        case BlendMode::Lighten: blendKey = "lite"; break;
        }
        records.insert(records.end(), blendKey, blendKey + 4);
        records.push_back(layer.opacity);
        records.push_back(0);
        records.push_back(std::uint8_t(0x08u | (layer.visible ? 0u : 0x02u)));
        records.push_back(0);

        Bytes extra;
        if (hasMask) {
            put32(extra, 20);
            put32s(extra, std::int32_t(layer.maskBounds.y));
            put32s(extra, std::int32_t(layer.maskBounds.x));
            put32s(extra, std::int32_t(layer.maskBounds.y +
                                       std::int64_t(layer.maskBounds.height)));
            put32s(extra, std::int32_t(layer.maskBounds.x +
                                       std::int64_t(layer.maskBounds.width)));
            extra.push_back(layer.maskDefault);
            extra.push_back(0);
            extra.push_back(0);
            extra.push_back(0);
        } else {
            put32(extra, 0);
        }
        put32(extra, 0);
        const auto fallbackLength = std::min<std::size_t>(255, layer.name.size());
        extra.push_back(std::uint8_t(fallbackLength));
        extra.insert(extra.end(), layer.name.begin(),
                     layer.name.begin() + std::ptrdiff_t(fallbackLength));
        while (extra.size() % 4)
            extra.push_back(0);
        auto unicode = appendUnicodeName(extra, layer.name);
        if (!unicode)
            return unicode.error();
        if (layer.sectionType) {
            if (*layer.sectionType > 3)
                return error(ErrorCode::InvalidInput, "psd.section-divider-type");
            extra.insert(extra.end(), {'8', 'B', 'I', 'M', 'l', 's', 'c', 't'});
            put32(extra, 4);
            put32(extra, *layer.sectionType);
        }
        if (extra.size() > UINT32_MAX)
            return error(ErrorCode::BudgetExceeded, "psd.layer-extra-size");
        put32(records, std::uint32_t(extra.size()));
        records.insert(records.end(), extra.begin(), extra.end());
    }

    auto rawLayerInfo = checkedAdd(std::uint64_t(records.size()), channelDataBytes);
    if (!rawLayerInfo)
        return rawLayerInfo.error();
    const auto layerInfoBytes =
        rawLayerInfo.value() + (rawLayerInfo.value() & 1u);
    if (layerInfoBytes > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.layer-info-size");
    const auto transparencyBlock =
        request.format.alpha && request.layers.empty()
            ? mergedTransparencyBlock(request.format.depth)
            : Bytes{};
    auto layerMaskBytes = checkedAdd(layerInfoBytes, 8);
    if (!layerMaskBytes)
        return layerMaskBytes.error();
    layerMaskBytes = checkedAdd(layerMaskBytes.value(), transparencyBlock.size());
    if (!layerMaskBytes || layerMaskBytes.value() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.layer-mask-size");

    auto mergedPayload = checkedMultiply(planeBytes.value(), channelCount);
    if (!mergedPayload)
        return mergedPayload.error();
    auto exactTotal = checkedAdd(std::uint64_t(26 + 4 + 4 + 4 + 2),
                                 std::uint64_t(resources.value().size()));
    if (!exactTotal)
        return exactTotal.error();
    exactTotal = checkedAdd(exactTotal.value(), layerMaskBytes.value());
    if (!exactTotal)
        return exactTotal.error();
    exactTotal = checkedAdd(exactTotal.value(), mergedPayload.value());
    if (!exactTotal)
        return exactTotal.error();
    if (exactTotal.value() > 2 * GiB)
        return error(ErrorCode::BudgetExceeded, "psd.output-size-limit");

    static std::atomic<std::uint64_t> serial{};
    auto stagedPath = request.path;
    stagedPath += std::string(".stage-") +
                  std::to_string(
                      std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(serial++);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{stagedPath};

    NativeOutputFile out;
    auto opened = out.createExclusive(stagedPath);
    if (!opened)
        return opened.error();

    Sha256State digest;
    std::uint64_t stagedBytes = 0;
    std::uint64_t peakBuffer = 0;
    auto observe = [&](std::uint64_t n) {
        peakBuffer = std::max(peakBuffer, n);
    };
    observe(resources.value().size());
    observe(records.size());

    auto emit = [&](std::span<const std::uint8_t> bytes) -> Result<void> {
        if (stop.stop_requested())
            return error(ErrorCode::Cancelled, "psd.cancelled");
        auto written = out.write(bytes);
        if (!written)
            return written.error();
        digest.update(bytes);
        auto next = checkedAdd(stagedBytes, bytes.size());
        if (!next)
            return next.error();
        stagedBytes = next.value();
        return {};
    };
    auto emitBytes = [&](const Bytes& bytes) -> Result<void> {
        return emit(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    };

    Bytes header;
    header.insert(header.end(), {'8', 'B', 'P', 'S'});
    put16(header, 1);
    header.insert(header.end(), 6, 0);
    put16(header, std::uint16_t(channelCount));
    put32(header, std::uint32_t(request.size.height));
    put32(header, std::uint32_t(request.size.width));
    put16(header, std::uint16_t(request.format.depth));
    put16(header, request.format.mode == ColorMode::RGB ? 3 : 4);
    put32(header, 0);
    put32(header, std::uint32_t(resources.value().size()));
    observe(header.size());
    auto emitted = emitBytes(header);
    if (!emitted)
        return emitted.error();
    emitted = emitBytes(resources.value());
    if (!emitted)
        return emitted.error();

    Bytes sectionHeader;
    put32(sectionHeader, std::uint32_t(layerMaskBytes.value()));
    put32(sectionHeader, std::uint32_t(layerInfoBytes));
    emitted = emitBytes(sectionHeader);
    if (!emitted)
        return emitted.error();
    emitted = emitBytes(records);
    if (!emitted)
        return emitted.error();

    std::vector<std::uint8_t> row;
    try {
        row.resize(std::size_t(request.size.width) * sampleBytes);
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "psd.row-buffer");
    }
    observe(row.size());
    const std::array<std::uint8_t, 2> rawCompression{0, 0};

    auto emitPixelChannels = [&](const TileData& pixels,
                                 bool perChannelCompression,
                                 bool matteMergedPreview) -> Result<void> {
        for (unsigned channel = 0; channel < channelCount; ++channel) {
            if (perChannelCompression) {
                auto compression = emit(rawCompression);
                if (!compression)
                    return compression.error();
            }
            for (std::uint32_t y = 0; y < pixels.height; ++y) {
                if (stop.stop_requested())
                    return error(ErrorCode::Cancelled, "psd.cancelled");
                for (std::uint32_t x = 0; x < pixels.width; ++x) {
                    const auto offset = std::size_t(
                        std::uint64_t(y) * pixels.rowStride +
                        std::uint64_t(x) * channelCount * sampleBytes +
                        std::uint64_t(channel) * sampleBytes);
                    if (offset + sampleBytes > pixels.bytes.size())
                        return error(ErrorCode::CorruptData, "psd.pixel-layout");
                    if (sampleBytes == 1) {
                        auto value = pixels.bytes[offset];
                        if (matteMergedPreview &&
                            request.format.mode == ColorMode::RGB &&
                            request.format.alpha && channel < baseChannels) {
                            const auto alphaOffset = offset +
                                std::size_t(baseChannels - channel);
                            if (alphaOffset >= pixels.bytes.size())
                                return error(ErrorCode::CorruptData, "psd.merged-alpha");
                            value = std::uint8_t(whiteMatteSample(
                                value, pixels.bytes[alphaOffset], 255u));
                        }
                        if (request.format.mode == ColorMode::CMYK &&
                            channel < baseChannels)
                            value = std::uint8_t(255u - value);
                        row[x] = value;
                    } else {
                        auto value = std::uint16_t(
                            pixels.bytes[offset] |
                            (std::uint16_t(pixels.bytes[offset + 1]) << 8));
                        if (matteMergedPreview &&
                            request.format.mode == ColorMode::RGB &&
                            request.format.alpha && channel < baseChannels) {
                            const auto alphaOffset = offset +
                                std::size_t(baseChannels - channel) * 2u;
                            if (alphaOffset + 1 >= pixels.bytes.size())
                                return error(ErrorCode::CorruptData, "psd.merged-alpha");
                            const auto alpha = std::uint16_t(
                                pixels.bytes[alphaOffset] |
                                (std::uint16_t(pixels.bytes[alphaOffset + 1]) << 8));
                            value = std::uint16_t(
                                whiteMatteSample(value, alpha, 65535u));
                        }
                        if (request.format.mode == ColorMode::CMYK &&
                            channel < baseChannels)
                            value = std::uint16_t(65535u - value);
                        row[std::size_t(x) * 2] = std::uint8_t(value >> 8);
                        row[std::size_t(x) * 2 + 1] = std::uint8_t(value);
                    }
                }
                auto line = emit(
                    std::span<const std::uint8_t>(row.data(), row.size()));
                if (!line)
                    return line.error();
            }
        }
        return {};
    };

    for (const auto& layer : request.layers) {
        auto pixels = emitPixelChannels(layer.pixels, true, false);
        if (!pixels)
            return pixels.error();
        if (!layer.mask.empty()) {
            auto compression = emit(rawCompression);
            if (!compression)
                return compression.error();
            const auto maskRowBytes =
                std::size_t(layer.maskBounds.width) * sampleBytes;
            if (row.size() < maskRowBytes) {
                try {
                    row.resize(maskRowBytes);
                } catch (const std::bad_alloc&) {
                    return error(ErrorCode::BudgetExceeded, "psd.row-buffer");
                }
                observe(row.size());
            }
            for (std::uint64_t y = 0; y < layer.maskBounds.height; ++y) {
                if (stop.stop_requested())
                    return error(ErrorCode::Cancelled, "psd.cancelled");
                for (std::uint64_t x = 0; x < layer.maskBounds.width; ++x) {
                    const auto value =
                        layer.mask[std::size_t(y * layer.maskBounds.width + x)];
                    if (sampleBytes == 1) {
                        row[std::size_t(x)] = value;
                    } else {
                        row[std::size_t(x) * 2] = value;
                        row[std::size_t(x) * 2 + 1] = value;
                    }
                }
                auto line = emit(
                    std::span<const std::uint8_t>(row.data(), maskRowBytes));
                if (!line)
                    return line.error();
            }
        }
    }

    if ((rawLayerInfo.value() & 1u) != 0) {
        const std::array<std::uint8_t, 1> zero{0};
        emitted = emit(zero);
        if (!emitted)
            return emitted.error();
    }
    const std::array<std::uint8_t, 4> globalMaskLength{0, 0, 0, 0};
    emitted = emit(globalMaskLength);
    if (!emitted)
        return emitted.error();
    if (!transparencyBlock.empty()) {
        emitted = emitBytes(transparencyBlock);
        if (!emitted)
            return emitted.error();
    }

    emitted = emit(rawCompression);
    if (!emitted)
        return emitted.error();
    auto composite = emitPixelChannels(*merged, false, true);
    if (!composite)
        return composite.error();

    auto closed = out.close();
    if (!closed)
        return closed.error();

    if (stats) {
        stats->stagedBytes = stagedBytes;
        stats->peakWriterBufferBytes = peakBuffer;
    }
    if (stagedBytes != exactTotal.value())
        return error(ErrorCode::CorruptData, "psd.stream-size-mismatch");

    NativeFileIO io;
    StagedExport staged{stagedPath, digest.finish()};
    DestinationPolicy policy{request.path, request.overwrite, true};
    return commitExport(io, staged, policy, 2 * GiB);
}

} // namespace drawborad
