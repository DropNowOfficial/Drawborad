#include "Test.h"
#include "models/ModelManifest.h"
#include <cstdlib>
#include <cstring>
#include <limits>
using namespace drawborad;

namespace {
template<class T>
Bytes bytesOf(std::initializer_list<T> values) {
    Bytes out(values.size()*sizeof(T));
    std::memcpy(out.data(), values.begin(), out.size());
    return out;
}
}

TEST("M02 two-stage SAM runtime chains encoder and decoder with int64 labels") {
    const char* encoder = std::getenv("DRAWBORAD_TEST_SAM_ENCODER");
    const char* decoder = std::getenv("DRAWBORAD_TEST_SAM_DECODER");
    CHECK(encoder != nullptr);
    CHECK(decoder != nullptr);

    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.resolvedWeights = encoder;
    manifest.resolvedDecoderWeights = decoder;
    manifest.provider = "cpu";
    manifest.inputs = {
        {"image","float32",{1,4},16},
        {"point_coords","float32",{1,1,2},8},
        {"point_labels","int64",{1,1},8},
        {"mask_input","float32",{1,1,2,2},16},
        {"has_mask_input","float32",{1},4}
    };
    manifest.outputs = {
        {"low_res_masks","float32",{1,4},16},
        {"iou_scores","float32",{1},4}
    };

    OrtSession session(manifest);
    CHECK(session.runtimeAvailable());

    auto image = bytesOf<float>({0.25f,-1.5f,7.0f,3.25f});
    auto pointCoords = bytesOf<float>({2.0f,3.0f});
    auto pointLabels = bytesOf<std::int64_t>({1});
    auto mask = bytesOf<float>({0,0,0,0});
    auto hasMask = bytesOf<float>({0.75f});

    InferenceRequest request;
    request.modelId = Id::fromNumber(22);
    request.device = DevicePreference::CPU;
    request.tensorRefs = {image,pointCoords,pointLabels,mask,hasMask};

    auto result = session.run(request);
    if (!result) throw std::runtime_error(result.error().messageKey + ": " + result.error().diagnosticId);
    CHECK(result.value().tensorRefs.size()==2);
    CHECK(result.value().tensorRefs[0]==image);
    CHECK(result.value().tensorRefs[1]==hasMask);
    CHECK(result.value().peakBytes >= image.size()+pointCoords.size()+pointLabels.size()+mask.size()+hasMask.size());
}

TEST("M02 two-stage SAM runtime rejects missing decoder path") {
    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.resolvedWeights = std::getenv("DRAWBORAD_TEST_SAM_ENCODER");
    manifest.inputs = {
        {"image","float32",{1,4},16},
        {"point_coords","float32",{1,1,2},8},
        {"point_labels","int64",{1,1},8},
        {"mask_input","float32",{1,1,2,2},16},
        {"has_mask_input","float32",{1},4}
    };
    manifest.outputs = {{"low_res_masks","float32",{1,4},16},{"iou_scores","float32",{1},4}};
    OrtSession session(manifest);
    InferenceRequest request;
    request.modelId=Id::fromNumber(23);
    request.tensorRefs={Bytes(16),Bytes(8),Bytes(8),Bytes(16),Bytes(4)};
    auto result=session.run(request);
    CHECK(!result);
}

namespace {
ModelManifest smallRuntimeManifest() {
    const auto* encoder = std::getenv("DRAWBORAD_TEST_SAM_ENCODER");
    const auto* decoder = std::getenv("DRAWBORAD_TEST_SAM_DECODER");
    CHECK(encoder != nullptr);
    CHECK(decoder != nullptr);
    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.resolvedWeights = encoder;
    manifest.resolvedDecoderWeights = decoder;
    manifest.provider = "cpu";
    manifest.inputs = {
        {"image", "float32", {1, 4}, 16},
        {"point_coords", "float32", {1, 1, 2}, 8},
        {"point_labels", "int64", {1, 1}, 8},
        {"mask_input", "float32", {1, 1, 2, 2}, 16},
        {"has_mask_input", "float32", {1}, 4}
    };
    manifest.outputs = {
        {"low_res_masks", "float32", {1, 4}, 16},
        {"iou_scores", "float32", {1}, 4}
    };
    return manifest;
}

InferenceRequest smallRuntimeRequest() {
    return {Id::fromNumber(24), {
        bytesOf<float>({0.25f, -1.5f, 7.0f, 3.25f}),
        bytesOf<float>({2, 3}), bytesOf<std::int64_t>({1}),
        bytesOf<float>({0, 0, 0, 0}), bytesOf<float>({0.75f})
    }, DevicePreference::CPU};
}
}

TEST("M02 runtime rejects an output rank mismatch even when its byte count agrees") {
    auto manifest = smallRuntimeManifest();
    manifest.outputs[0].shape = {1, 2, 2};
    const auto result = OrtSession(manifest).run(smallRuntimeRequest());
    CHECK(!result);
    CHECK(result.error().messageKey == "model.runtime-output-shape");
}

TEST("M02 runtime rejects a fixed output dimension mismatch") {
    auto manifest = smallRuntimeManifest();
    manifest.outputs[0].shape = {1, 5};
    const auto result = OrtSession(manifest).run(smallRuntimeRequest());
    CHECK(!result);
    CHECK(result.error().messageKey == "model.runtime-output-shape");
}

TEST("M02 runtime permits dynamic output dimensions only when the concrete result fits") {
    auto manifest = smallRuntimeManifest();
    manifest.outputs[0].shape = {1, -1};
    auto request = smallRuntimeRequest();
    const auto result = OrtSession(manifest).run(request);
    CHECK(result);
    CHECK(result.value().tensorRefs[0] == request.tensorRefs[0]);
    manifest.outputs[0].shape = {1, -2};
    CHECK(!OrtSession(manifest).run(request));
}

TEST("M02 runtime rejects zero output dimensions") {
    auto manifest = smallRuntimeManifest();
    manifest.outputs[0].shape = {1, 0};
    CHECK(!OrtSession(manifest).run(smallRuntimeRequest()));
}

TEST("M02 runtime checks resolved input dimensions against the actual model interface") {
    auto manifest = smallRuntimeManifest();
    manifest.inputs[0].shape = {2, 2};
    const auto result = OrtSession(manifest).run(smallRuntimeRequest());
    CHECK(!result);
    CHECK(result.error().messageKey == "model.runtime-input-shape");
}

TEST("M02 runtime accepts minus one input dimensions but rejects other negative dimensions") {
    auto manifest = smallRuntimeManifest();
    manifest.inputs[0].shape = {1, -1};
    CHECK(OrtSession(manifest).run(smallRuntimeRequest()));
    manifest.inputs[0].shape = {1, -2};
    CHECK(!OrtSession(manifest).run(smallRuntimeRequest()));
}

TEST("M02 runtime rejects nonfinite decoder outputs") {
    auto request = smallRuntimeRequest();
    request.tensorRefs[4] = bytesOf<float>({std::numeric_limits<float>::infinity()});
    const auto result = OrtSession(smallRuntimeManifest()).run(request);
    CHECK(!result);
    CHECK(result.error().messageKey == "model.nonfinite-output");
}
