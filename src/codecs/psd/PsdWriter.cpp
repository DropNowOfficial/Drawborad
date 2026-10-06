#include "PsdWriter.h"
#include "storage/FileIO.h"
#include <algorithm>
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

Result<Bytes> encodePsd(const PsdWriteRequest& request) {
    if (!request.size.width || !request.size.height || request.size.width > 30000 ||
        request.size.height > 30000)
        return error(ErrorCode::UnsupportedFeature, "psd.dimension-limit");
    if (request.format.depth != Depth::U8 && request.format.depth != Depth::U16)
        return error(ErrorCode::UnsupportedFeature, "psd.depth");
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
    put32(output, 0);

    Bytes records;
    put16(records, std::uint16_t(request.layers.size()));

    for (const auto& layer : request.layers) {
        if (layer.pixels.width != request.size.width || layer.pixels.height != request.size.height ||
            layer.pixels.format != request.format)
            return error(ErrorCode::InvalidInput, "psd.layer-layout");

        const auto expectedRow =
            request.size.width * std::uint64_t(channelCount) * std::uint64_t(sampleBytes);
        if (layer.pixels.rowStride < expectedRow)
            return error(ErrorCode::CorruptData, "psd.layer-stride");

        put32s(records, 0);
        put32s(records, 0);
        put32(records, std::uint32_t(request.size.height));
        put32(records, std::uint32_t(request.size.width));
        put16(records, std::uint16_t(channelCount));

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
        records.push_back(0);
        records.push_back(0);

        Bytes extra;
        put32(extra, 0);
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
                    channelData.insert(channelData.end(),
                                       layer.pixels.bytes.begin() + std::ptrdiff_t(offset),
                                       layer.pixels.bytes.begin() +
                                           std::ptrdiff_t(offset + sampleBytes));
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

    if (layerMask.size() > UINT32_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.layer-mask-size");
    put32(output, std::uint32_t(layerMask.size()));
    output.insert(output.end(), layerMask.begin(), layerMask.end());

    put16(output, 0);
    const auto pixelCount = checkedMultiply(request.size.width, request.size.height);
    if (!pixelCount)
        return pixelCount.error();
    const auto compositeBytes = checkedMultiply(pixelCount.value(), sampleBytes);
    if (!compositeBytes)
        return compositeBytes.error();
    const auto allComposite = checkedMultiply(compositeBytes.value(), baseChannels);
    if (!allComposite || allComposite.value() > SIZE_MAX)
        return error(ErrorCode::BudgetExceeded, "psd.composite-size");
    output.insert(output.end(), std::size_t(allComposite.value()), 0);

    return output;
}

Result<void> writePsd(const PsdWriteRequest& request, std::stop_token stop) {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "psd.cancelled");
    auto encoded = encodePsd(request);
    if (!encoded)
        return encoded.error();
    NativeFileIO io;
    return atomicWrite(io, request.path, encoded.value());
}

} // namespace drawborad
