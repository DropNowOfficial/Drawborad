#pragma once
#include "ModelManifest.h"
namespace drawborad {
struct AdapterInfo { std::string id; std::string abi; std::vector<TensorSpec> inputs,outputs; };
struct CompatibilityReport { bool supported{}; std::string reason; std::uint64_t peakRam{},peakGpu{}; DevicePreference device{DevicePreference::CPU}; };
class AdapterRegistry { public: Result<AdapterInfo> resolve(std::string_view task) const; };
Result<CompatibilityReport> probeModel(const ModelManifest&,DevicePreference,std::stop_token={});
}
