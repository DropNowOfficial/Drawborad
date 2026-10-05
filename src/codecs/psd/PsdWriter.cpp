#include "PsdWriter.h"
#include "storage/FileIO.h"
#include <algorithm>

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
        const auto nameLength = std::min<std::size_t>(255, layer.name.size());
        extra.push_back(std::uint8_t(nameLength));
        extra.insert(extra.end(), layer.name.begin(), layer.name.begin() + std::ptrdiff_t(nameLength));
        while (extra.size() % 4)
            extra.push_back(0);

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
