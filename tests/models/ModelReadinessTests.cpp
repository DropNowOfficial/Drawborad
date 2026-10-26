#include "Test.h"
#include "models/AdapterRegistry.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace drawborad;

namespace {
struct PackageFixture {
    std::filesystem::path root;
    Bytes encoder{0x3a, 0x00};
    Bytes decoder{0x3a, 0x00};

    PackageFixture() {
        static std::atomic<unsigned> next{};
        root = std::filesystem::temp_directory_path() /
            ("drawborad-readiness-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(next++));
        std::filesystem::create_directories(root);
        write(root / "encoder.onnx", encoder);
        write(root / "decoder.onnx", decoder);
        manifest();
    }
    ~PackageFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    static void write(const std::filesystem::path& path, const Bytes& bytes) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    }
    void manifest(std::string_view extra = {},
                  std::string_view task = "sam21.image.v1") const {
        std::ofstream out(root / "manifest.json");
        out << "{\"schema\":1,\"taskId\":\"" << task << "\","
               "\"weights\":\"encoder.onnx\",\"sha256\":\"" << sha256(encoder).hex()
            << "\",\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""
            << sha256(decoder).hex() << "\",\"license\":\"test\"" << extra << "}";
    }
};
}

TEST("readiness rejects a nonstring provider instead of silently choosing CPU") {
    PackageFixture package;
    package.manifest(",\"provider\":7");
    const auto result = validatePackage(package.root);
    CHECK(!result);
    CHECK(result.error().messageKey == "model.manifest-field-type");
}

TEST("readiness rejects a nonstring ABI instead of silently choosing the SAM adapter") {
    PackageFixture package;
    package.manifest(",\"abi\":false");
    const auto result = validatePackage(package.root);
    CHECK(!result);
    CHECK(result.error().messageKey == "model.manifest-field-type");
}

TEST("readiness rejects a nonboolean network policy") {
    PackageFixture package;
    package.manifest(",\"networkAccess\":\"false\"");
    const auto result = validatePackage(package.root);
    CHECK(!result);
    CHECK(result.error().messageKey == "model.manifest-field-type");
}

TEST("verified packages preserve provenance and bind the exact manifest contents") {
    PackageFixture package;
    package.manifest(",\"provider\":\"cpu\",\"abi\":\"sam21.image.v1\","
        "\"source\":\"facebookresearch/sam2\",\"sourceCommit\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"checkpointSha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"variant\":\"tiny\",\"networkAccess\":false");
    const auto result = validatePackage(package.root);
    CHECK(result);
    const auto& manifest = result.value();
    CHECK(manifest.source == "facebookresearch/sam2");
    CHECK(manifest.sourceCommit == std::string(40, 'a'));
    CHECK(manifest.checkpointSha256 == std::string(64, 'b'));
    CHECK(manifest.variant == "tiny");
    CHECK(!manifest.networkAccess);
    CHECK(manifest.license == "test");
    CHECK(manifest.taskId == "sam21.image.v1");
    CHECK(manifest.abi == "sam21.image.v1");
    CHECK(manifest.sha256 == sha256(package.encoder));
    CHECK(manifest.decoderSha256 == sha256(package.decoder));
    std::ifstream file(package.root / "manifest.json", std::ios::binary);
    const Bytes bytes((std::istreambuf_iterator<char>(file)), {});
    CHECK(manifest.manifestSha256 == sha256(bytes));
}

TEST("readiness reports no selected package as MissingModel") {
    const auto report = preflightModelPackage({});
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::MissingModel);
    CHECK(!report.value().manifest);
    CHECK(report.value().probeKind.empty());
}

TEST("readiness reports an absent package as MissingModel") {
    PackageFixture package;
    const auto report = preflightModelPackage(package.root / "absent");
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::MissingModel);
}

TEST("readiness reports a missing manifest as InvalidManifest") {
    PackageFixture package;
    std::filesystem::remove(package.root / "manifest.json");
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::InvalidManifest);
    CHECK(report.value().messageKey == "model.manifest");
}

TEST("readiness reports malformed provider metadata as InvalidManifest") {
    PackageFixture package;
    package.manifest(",\"provider\":[]");
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::InvalidManifest);
    CHECK(report.value().diagnostic == "provider");
}

TEST("readiness reports decoder tampering as ModelIntegrityFailed before runtime") {
    PackageFixture package;
    PackageFixture::write(package.root / "decoder.onnx", Bytes{0x3a, 0x01, 0x00});
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::ModelIntegrityFailed);
    CHECK(report.value().messageKey == "model.decoder-digest");
    CHECK(!report.value().manifest);
    CHECK(report.value().probeKind.empty());
}

TEST("readiness reports missing local weights as ModelIntegrityFailed") {
    PackageFixture package;
    std::filesystem::remove(package.root / "encoder.onnx");
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::ModelIntegrityFailed);
}

TEST("readiness reports an unsupported adapter as IncompatibleModel") {
    PackageFixture package;
    package.manifest({}, "unknown.task");
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::IncompatibleModel);
}

TEST("readiness reports an undeclared requested provider as ProviderUnavailable") {
    PackageFixture package;
    const auto report = preflightModelPackage(package.root, DevicePreference::CUDA);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::ProviderUnavailable);
    CHECK(report.value().device == DevicePreference::CUDA);
    CHECK(report.value().messageKey == "model.cuda-not-declared");
}

TEST("readiness reports an unsupported declared provider as ProviderUnavailable") {
    PackageFixture package;
    package.manifest(",\"provider\":\"unavailable-provider\"");
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state == ModelReadinessState::ProviderUnavailable);
}

TEST("readiness never promotes package hashes or manifest-only compatibility to Ready") {
    PackageFixture package;
    const auto report = preflightModelPackage(package.root);
    CHECK(report);
    CHECK(report.value().state != ModelReadinessState::Ready);
    CHECK(report.value().manifest);
    CHECK(!report.value().manifest->manifestSha256.empty());
    CHECK(report.value().manifest->decoderSha256 == sha256(package.decoder));
    OrtSession session(*report.value().manifest);
    if (!session.runtimeAvailable()) {
        CHECK(report.value().state == ModelReadinessState::RuntimeUnavailable);
        CHECK(report.value().messageKey == "model.onnxruntime-unavailable");
        CHECK(report.value().probeKind.empty());
    } else {
        // The bytes are storage fixtures, not executable ONNX models.
        CHECK(report.value().state == ModelReadinessState::IncompatibleModel);
        CHECK(report.value().probeKind == "deterministic-smoke");
    }
}

TEST("cancelled readiness checks cannot publish a readiness report") {
    PackageFixture package;
    std::stop_source stop;
    stop.request_stop();
    const auto report = preflightModelPackage(package.root, DevicePreference::CPU, stop.get_token());
    CHECK(!report);
    CHECK(report.error().code == ErrorCode::Cancelled);
}
