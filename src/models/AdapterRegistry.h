#pragma once
#include "ModelManifest.h"
#include <string_view>
namespace drawborad {
struct AdapterInfo { std::string id; std::string abi; std::vector<TensorSpec> inputs,outputs; };
// Legacy compatibility may describe a manifest alone. Executable readiness is
// reported only by preflightModelPackage after a fresh two-stage inference.
struct CompatibilityReport { bool supported{}; std::string reason; std::uint64_t peakRam{},peakGpu{}; DevicePreference device{DevicePreference::CPU}; };
class AdapterRegistry { public: Result<AdapterInfo> resolve(std::string_view task) const; };
Result<CompatibilityReport> probeModel(const ModelManifest&,DevicePreference,std::stop_token={});
Result<CompatibilityReport> probeModel(const ModelRegistry&,ModelId,DevicePreference,std::stop_token={});
enum class ModelReadinessState {
    MissingModel,
    InvalidManifest,
    ModelIntegrityFailed,
    RuntimeUnavailable,
    ProviderUnavailable,
    IncompatibleModel,
    Ready
};
struct ModelReadinessReport {
    ModelReadinessState state{ModelReadinessState::MissingModel};
    std::string messageKey;
    std::string diagnostic;
    DevicePreference device{DevicePreference::CPU};
    std::optional<ModelManifest> manifest;
    std::uint64_t elapsedUs{}, observedTensorBytes{};
    // A deterministic smoke proves the local runtime contract, not model quality
    // or a successful review of the user's image.
    std::string probeKind;
};
std::string_view modelReadinessStateName(ModelReadinessState);
Result<ModelReadinessReport> preflightModelPackage(
    const PackagePath&, DevicePreference = DevicePreference::CPU, std::stop_token = {});
}
