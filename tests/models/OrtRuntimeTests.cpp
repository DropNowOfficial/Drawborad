#include "Test.h"
#include "models/ModelManifest.h"
#include <cstdlib>
#include <cstring>
using namespace drawborad;

TEST("M02 real ONNX Runtime executes a verified float32 model") {
    const char* model = std::getenv("DRAWBORAD_TEST_ONNX_MODEL");
    CHECK(model != nullptr);
    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.resolvedWeights = model;
    manifest.provider = "cpu";
    manifest.inputs = {{"image", "float32", {1, 4}, 16}};
    manifest.outputs = {{"output", "float32", {1, 4}, 16}};
    OrtSession session(manifest);
    CHECK(session.runtimeAvailable());

    const float values[4] = {0.25f, -1.5f, 7.0f, 3.25f};
    Bytes input(sizeof(values));
    std::memcpy(input.data(), values, sizeof(values));
    InferenceRequest request;
    request.modelId = Id::fromNumber(1);
    request.device = DevicePreference::CPU;
    request.tensorRefs = {input};

    auto output = session.run(request);
    if (!output) throw std::runtime_error(output.error().messageKey + ": " + output.error().diagnosticId);
    CHECK(output.value().tensorRefs.size() == 1);
    CHECK(output.value().tensorRefs[0] == input);
    CHECK(output.value().peakBytes >= input.size() * 2);
}

TEST("M02 CUDA is never silently replaced by CPU") {
    const char* model = std::getenv("DRAWBORAD_TEST_ONNX_MODEL");
    CHECK(model != nullptr);
    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.resolvedWeights = model;
    manifest.provider = "cuda";
    OrtSession session(manifest);
    InferenceRequest request;
    request.modelId = Id::fromNumber(2);
    request.device = DevicePreference::CUDA;
    request.tensorRefs = {Bytes(16)};
    auto result = session.run(request);
    CHECK(!result);
    CHECK(result.error().code == ErrorCode::UnsupportedFeature);
}
