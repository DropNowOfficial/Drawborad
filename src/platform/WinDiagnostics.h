#pragma once
#include "core/Types.h"
#include <optional>
namespace drawborad {
struct GpuAdapter {
    std::string name, luid, driver;
    std::uint64_t dedicatedBytes{};
    bool software = false;
};
struct CudaDevice {
    int ordinal{};
    std::string name, luid;
};
struct DeviceReport {
    std::string osBuild, cpu, driver, powerMode, sceneGraph = "NotInitialized";
    std::uint64_t ram{};
    std::uint32_t logicalThreads{}, dpi = 96;
    std::vector<GpuAdapter> gpuAdapters;
    std::optional<std::string> activeRenderLuid;
    std::vector<CudaDevice> cudaDevices;
    std::vector<std::string> webviewModules;
    bool moduleScanCompleted = false;
    std::vector<std::string> warnings;
};
DeviceReport collectDeviceReport();
std::string diagnosticJson(const DeviceReport&);
} // namespace drawborad
