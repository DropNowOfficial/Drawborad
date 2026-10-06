#pragma once
#include "core/Digest.h"
#include <filesystem>
#include <map>
#include <optional>
#include <stop_token>
namespace drawborad {
using PackagePath = std::filesystem::path;
enum class TrustDecision { TrustedSignature, UserAcceptedUntrusted };
enum class DevicePreference { CUDA, CPU };
struct TensorSpec { std::string name; std::string dtype; std::vector<std::int64_t> shape; std::uint64_t byteLimit{}; };
struct ModelManifest {
    std::uint32_t schema{1};
    std::string taskId;
    std::filesystem::path weights;
    std::filesystem::path resolvedWeights;
    Hash sha256{};
    std::filesystem::path decoderWeights;
    std::filesystem::path resolvedDecoderWeights;
    Hash decoderSha256{};
    std::vector<TensorSpec> inputs, outputs;
    std::string provider;
    std::string abi;
    std::string license;
    std::uint64_t gpuBytes{}, ramBytes{};
    bool signedPackage{};
};
struct InferenceRequest { ModelId modelId{}; std::vector<Bytes> tensorRefs; DevicePreference device{DevicePreference::CPU}; };
struct InferenceOutput { std::vector<Bytes> tensorRefs; std::uint64_t elapsedUs{}, peakBytes{}; };
Result<ModelManifest> validatePackage(const PackagePath&);
class ModelRegistry {
    std::map<ModelId,ModelManifest> installed_;
    std::map<ModelId,std::filesystem::path> roots_;
    std::map<ModelId,std::uint32_t> active_;
  public:
    Result<ModelId> install(const PackagePath&, TrustDecision);
    Result<void> remove(ModelId);
    Result<ModelManifest> manifest(ModelId) const;
    Result<void> retain(ModelId);
    void release(ModelId);
};
class OrtSession {
    ModelManifest manifest_;
  public:
    explicit OrtSession(ModelManifest m):manifest_(std::move(m)){}
    Result<InferenceOutput> run(const InferenceRequest&, std::stop_token = {}) const;
    bool runtimeAvailable() const;
};
}
