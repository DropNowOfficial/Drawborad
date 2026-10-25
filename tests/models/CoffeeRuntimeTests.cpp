#include "Test.h"
#include "codecs/ImageReader.h"
#include "models/ModelManifest.h"
#include "segmentation/ProposalEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <chrono>
using namespace drawborad;
namespace {
std::filesystem::path environment(const char* key) {
#ifdef _WIN32
    wchar_t* value = nullptr;
    const std::wstring name(key, key + std::strlen(key));
    if (_wdupenv_s(&value, nullptr, name.c_str()) != 0 || !value)
        throw std::runtime_error(std::string("missing fixture environment: ") + key);
    std::unique_ptr<wchar_t, decltype(&std::free)> owned(value, &std::free);
#else
    const auto value = std::getenv(key);
    if (!value) throw std::runtime_error(std::string("missing fixture environment: ") + key);
#endif
    return value;
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read fixture: " + path.string());
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}
void write(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    CHECK(stream.good());
}
template<class T> Bytes packed(std::initializer_list<T> values) {
    Bytes bytes(values.size() * sizeof(T));
    std::memcpy(bytes.data(), values.begin(), bytes.size());
    return bytes;
}
float at(const Bytes& bytes, std::size_t index) {
    float value{};
    std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
    return value;
}
ModelManifest manifest() {
    const auto result = validatePackage(environment("DRAWBORAD_COFFEE_PACKAGE"));
    if (!result) throw std::runtime_error(result.error().messageKey + ": " + result.error().diagnosticId);
    return result.value();
}
constexpr std::array<std::array<double, 2>, 3> points{{{280, 175}, {350, 278}, {180, 320}}};
}

TEST("M03 coffee real ORT logits match pinned official image predictor") {
    const auto model = manifest();
    const auto reference = environment("DRAWBORAD_COFFEE_REFERENCE");
    const auto output = environment("DRAWBORAD_COFFEE_OUTPUT");
    std::filesystem::create_directories(output);
    OrtSession session(model);
    CHECK(session.runtimeAvailable());
    const auto image = read(reference / "image.f32");
    double largestError = 0;
    for (std::size_t c = 0; c < points.size(); ++c) {
        InferenceRequest request;
        request.modelId = Id::fromNumber(1);
        request.tensorRefs = {image,
            packed<float>({float(points[c][0] * 1024 / 600), float(points[c][1] * 1024 / 400)}),
            packed<std::int64_t>({1}), Bytes(256 * 256 * sizeof(float), 0), packed<float>({0})};
        const auto result = session.run(request);
        if (!result) throw std::runtime_error(result.error().messageKey + ": " + result.error().diagnosticId);
        CHECK(result.value().tensorRefs.size() == 2);
        const auto expected = read(reference / ("case" + std::to_string(c) + "-low.f32"));
        const auto scores = read(reference / ("case" + std::to_string(c) + "-scores.f32"));
        const auto& actual = result.value().tensorRefs[0];
        CHECK(actual.size() == expected.size());
        CHECK(result.value().tensorRefs[1].size() == scores.size());
        for (std::size_t i = 0; i < actual.size() / sizeof(float); ++i)
            largestError = std::max(largestError, double(std::abs(std::clamp(at(actual, i), -32.0f, 32.0f) - at(expected, i))));
        for (std::size_t i = 0; i < scores.size() / sizeof(float); ++i)
            CHECK(std::abs(at(result.value().tensorRefs[1], i) - at(scores, i)) < .001f);
        write(output / ("case" + std::to_string(c) + "-ort-low.f32"), actual);
    }
    std::cout << "coffee native/reference maximum low-logit error=" << largestError << '\n';
    CHECK(largestError < .05);
}

TEST("M03 coffee native decode preprocessing and full-resolution masks match official predictor") {
    const auto model = manifest();
    const auto reference = environment("DRAWBORAD_COFFEE_REFERENCE");
    const auto output = environment("DRAWBORAD_COFFEE_OUTPUT");
    auto decoded = decodeImage(environment("DRAWBORAD_COFFEE_IMAGE"), DecodeOptions{});
    if (!decoded) throw std::runtime_error(decoded.error().messageKey);
    CHECK(decoded.value().decodedPixels);
    CHECK(decoded.value().info.orientedSize == PixelSize(600, 400));
    AnalysisRequest request;
    request.sourceSize = decoded.value().info.orientedSize;
    request.model = model;
    request.view.rgbTiles = {*decoded.value().decodedPixels};
    std::filesystem::create_directories(output);
    double minimumIou = 1;
    for (std::size_t c = 0; c < points.size(); ++c) {
        request.prompts.points = {{points[c][0], points[c][1], true}};
        const auto proposals = ProposalEngine{}.propose(request);
        if (!proposals) throw std::runtime_error(proposals.error().messageKey + ": " + proposals.error().diagnosticId);
        const auto masks = projectMasks(proposals.value(), {request.sourceSize, true});
        CHECK(masks);
        const auto expected = read(reference / ("case" + std::to_string(c) + "-masks.u8"));
        CHECK(masks.value().fullResolutionMasks.size() == 3);
        CHECK(expected.size() == 3 * 600 * 400);
        for (std::size_t m = 0; m < 3; ++m) {
            const auto& actual = masks.value().fullResolutionMasks[m];
            std::uint64_t intersection = 0, joined = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const bool a = actual[i] > 0, b = expected[m * actual.size() + i] > 0;
                intersection += a && b;
                joined += a || b;
            }
            const auto iou = joined ? double(intersection) / double(joined) : 1;
            minimumIou = std::min(minimumIou, iou);
            std::cout << "coffee prompt=" << c << " candidate=" << m << " maskIoU=" << iou << '\n';
            write(output / ("case" + std::to_string(c) + "-mask" + std::to_string(m) + ".u8"), actual);
        }
    }
    CHECK(minimumIou >= .995);
}

TEST("M03 loaded SAM session survives model-file removal and invalidates changed image features") {
    auto model = manifest();
    const auto directory = environment("DRAWBORAD_COFFEE_OUTPUT") /
        ("session-owned-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            // Only these test-owned copies; never the verified source package.
            std::error_code ec;
            std::filesystem::remove(directory / "encoder.onnx", ec);
            std::filesystem::remove(directory / "decoder.onnx", ec);
            std::filesystem::remove(directory, ec);
        }
    } cleanup{directory};
    std::filesystem::copy_file(model.resolvedWeights, directory / "encoder.onnx");
    std::filesystem::copy_file(model.resolvedDecoderWeights, directory / "decoder.onnx");
    model.resolvedWeights = directory / "encoder.onnx";
    model.resolvedDecoderWeights = directory / "decoder.onnx";
    OrtSession session(model);
    InferenceRequest request;
    request.modelId = Id::fromNumber(1);
    request.tensorRefs = {read(environment("DRAWBORAD_COFFEE_REFERENCE") / "image.f32"),
        packed<float>({280.0f * 1024 / 600, 175.0f * 1024 / 400}),
        packed<std::int64_t>({1}), Bytes(256 * 256 * sizeof(float), 0), packed<float>({0})};
    const auto original = session.run(request);
    CHECK(original);
    CHECK(std::filesystem::remove(model.resolvedWeights));
    CHECK(std::filesystem::remove(model.resolvedDecoderWeights));
    const auto repeated = session.run(request);
    if (!repeated) throw std::runtime_error(repeated.error().messageKey + ": " + repeated.error().diagnosticId);
    CHECK(repeated.value().tensorRefs == original.value().tensorRefs);

    std::fill(request.tensorRefs[0].begin(), request.tensorRefs[0].end(), std::uint8_t{0});
    const auto changed = session.run(request);
    CHECK(changed);
    CHECK(changed.value().tensorRefs != original.value().tensorRefs);
    OrtSession fresh(manifest());
    const auto expectedChanged = fresh.run(request);
    CHECK(expectedChanged);
    CHECK(changed.value().tensorRefs == expectedChanged.value().tensorRefs);
}
