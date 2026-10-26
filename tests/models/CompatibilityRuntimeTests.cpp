#include "Test.h"
#include "models/AdapterRegistry.h"
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
using namespace drawborad;

TEST("M05 resolved compatible model must pass an isolated runtime probe"){
    const char* encoder=std::getenv("DRAWBORAD_TEST_PROBE_ENCODER");
    const char* decoder=std::getenv("DRAWBORAD_TEST_PROBE_DECODER");
    CHECK(encoder!=nullptr);
    CHECK(decoder!=nullptr);
    AdapterRegistry registry;
    auto adapter=registry.resolve("sam21.image.v1");
    CHECK(adapter);
    ModelManifest manifest;
    manifest.taskId="sam21.image.v1";
    manifest.abi=adapter.value().abi;
    manifest.provider="cpu";
    manifest.inputs=adapter.value().inputs;
    manifest.outputs=adapter.value().outputs;
    manifest.resolvedWeights=encoder;
    manifest.resolvedDecoderWeights=decoder;
    manifest.sha256.bytes[0]=1;
    manifest.decoderSha256.bytes[0]=2;
    manifest.ramBytes=512*1024*1024;
    manifest.gpuBytes=0;
    auto report=probeModel(manifest,DevicePreference::CPU);
    CHECK(report);
    CHECK(report.value().supported);
    CHECK(report.value().reason=="runtime-probe-passed");
    CHECK(report.value().peakRam>0);
}


TEST("M05 CUDA compatibility probe fails closed when runtime has no CUDA EP"){
    const char* encoder=std::getenv("DRAWBORAD_TEST_PROBE_ENCODER");
    const char* decoder=std::getenv("DRAWBORAD_TEST_PROBE_DECODER");
    CHECK(encoder!=nullptr);
    CHECK(decoder!=nullptr);
    AdapterRegistry registry;
    auto adapter=registry.resolve("sam21.image.v1");
    CHECK(adapter);
    ModelManifest manifest;
    manifest.taskId="sam21.image.v1";
    manifest.abi=adapter.value().abi;
    manifest.provider="cuda";
    manifest.inputs=adapter.value().inputs;
    manifest.outputs=adapter.value().outputs;
    manifest.resolvedWeights=encoder;
    manifest.resolvedDecoderWeights=decoder;
    manifest.sha256.bytes[0]=1;
    manifest.decoderSha256.bytes[0]=2;
    manifest.ramBytes=512*1024*1024;
    manifest.gpuBytes=2*GiB;
    auto report=probeModel(manifest,DevicePreference::CUDA);
    CHECK(report);
    CHECK(!report.value().supported);
    CHECK(report.value().reason=="model.cuda-provider-unavailable");
}

namespace {
ModelManifest runtimeManifest() {
    const auto* encoder = std::getenv("DRAWBORAD_TEST_PROBE_ENCODER");
    const auto* decoder = std::getenv("DRAWBORAD_TEST_PROBE_DECODER");
    CHECK(encoder != nullptr);
    CHECK(decoder != nullptr);
    const auto adapter = AdapterRegistry{}.resolve("sam21.image.v1");
    CHECK(adapter);
    ModelManifest manifest;
    manifest.taskId = "sam21.image.v1";
    manifest.abi = adapter.value().abi;
    manifest.provider = "cpu";
    manifest.inputs = adapter.value().inputs;
    manifest.outputs = adapter.value().outputs;
    manifest.resolvedWeights = encoder;
    manifest.resolvedDecoderWeights = decoder;
    return manifest;
}

struct RuntimePackage {
    std::filesystem::path root;
    RuntimePackage() {
        const auto manifest = runtimeManifest();
        root = std::filesystem::temp_directory_path() / ("drawborad-runtime-preflight-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        std::filesystem::copy_file(manifest.resolvedWeights, root / "encoder.onnx");
        std::filesystem::copy_file(manifest.resolvedDecoderWeights, root / "decoder.onnx");
        std::ifstream encoderFile(root / "encoder.onnx", std::ios::binary);
        std::ifstream decoderFile(root / "decoder.onnx", std::ios::binary);
        const Bytes encoder((std::istreambuf_iterator<char>(encoderFile)), {});
        const Bytes decoder((std::istreambuf_iterator<char>(decoderFile)), {});
        std::ofstream output(root / "manifest.json");
        output << "{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"provider\":\"cpu\","
                  "\"abi\":\"sam21.image.v1\",\"license\":\"synthetic-test-fixture\","
                  "\"networkAccess\":false,\"weights\":\"encoder.onnx\",\"sha256\":\""
               << sha256(encoder).hex() << "\",\"decoderWeights\":\"decoder.onnx\","
                  "\"decoderSha256\":\"" << sha256(decoder).hex() << "\"}";
    }
    ~RuntimePackage() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
}

TEST("M05 package preflight reaches Ready only after a real two-stage deterministic smoke") {
    RuntimePackage package;
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    if (report.value().state != ModelReadinessState::Ready)
        throw std::runtime_error(report.value().messageKey + ": " + report.value().diagnostic);
    CHECK(report.value().probeKind == "deterministic-smoke");
    CHECK(report.value().device == DevicePreference::CPU);
    CHECK(report.value().observedTensorBytes >= 3 * 1024 * 1024 * sizeof(float));
    CHECK(report.value().manifest);
    CHECK(!report.value().manifest->manifestSha256.empty());
    CHECK(!report.value().manifest->sha256.empty());
    CHECK(!report.value().manifest->decoderSha256.empty());
}

TEST("M05 runtime rejects nonfinite encoder features even when decoder output is finite") {
    auto manifest = runtimeManifest();
    InferenceRequest request;
    request.modelId = Id::fromNumber(25);
    request.tensorRefs = {
        Bytes(3 * 1024 * 1024 * sizeof(float), 0), Bytes(2 * sizeof(float), 0),
        Bytes(sizeof(std::int64_t), 0), Bytes(256 * 256 * sizeof(float), 0),
        Bytes(sizeof(float), 0)
    };
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(request.tensorRefs[0].data(), &nan, sizeof(nan));
    const auto result = OrtSession(manifest).run(request);
    CHECK(!result);
    CHECK(result.error().messageKey == "model.nonfinite-output");
}
