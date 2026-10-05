#include "Protocol.h"
#include <algorithm>
namespace drawborad {
std::size_t ResourceGovernor::concurrentHeavyGpuJobs() const {
    std::size_t n = 0;
    for (auto& [id, r] : reservations_) {
        (void)id;
        if (r.heavy && r.device == ResourceDevice::GpuInference)
            ++n;
    }
    return n;
}
ResourceDecision ResourceGovernor::admit(const ResourceRequest& r) const {
    if (r.id.empty() || reservations_.contains(r.id) || int(r.device) < 0 || int(r.device) > 2)
        return {false, false, false, "resource.identity"};
    std::uint64_t ram = 0, gpu = 0, display = 0;
    for (auto& [id, q] : reservations_) {
        (void)id;
        auto a = checkedAdd(ram, q.ramBytes + (q.device == ResourceDevice::CPU ? q.bytes : 0));
        if (!a)
            return {false, false, true, "resource.overflow"};
        ram = a.value();
        if (q.device != ResourceDevice::CPU) {
            auto b = checkedAdd(gpu, q.bytes);
            if (!b)
                return {false, false, true, "resource.overflow"};
            gpu = b.value();
        }
        if (q.device == ResourceDevice::GpuDisplay)
            display += q.bytes;
    }
    auto needRam = checkedAdd(r.ramBytes, r.device == ResourceDevice::CPU ? r.bytes : 0);
    if (!needRam)
        return {false, false, true, "resource.overflow"};
    auto totalRam = checkedAdd(ram, needRam.value());
    if (!totalRam)
        return {false, false, true, "resource.overflow"};
    if (needRam.value() > freeRam_ || totalRam.value() > budget_.ramReclaimBytes)
        return {false, true, true, "resource.ram"};
    if (r.device != ResourceDevice::CPU) {
        auto total = checkedAdd(gpu, r.bytes);
        if (!total || r.bytes > freeGpu_ || total.value() > budget_.gpuTotalBytes)
            return {false, true, true, "resource.gpu"};
        if (r.device == ResourceDevice::GpuDisplay) {
            auto d = checkedAdd(display, r.bytes);
            if (!d || d.value() > budget_.gpuDisplayBytes)
                return {false, true, true, "resource.display-cache"};
        }
    }
    if (r.heavy && r.device == ResourceDevice::GpuInference &&
        concurrentHeavyGpuJobs() >= std::min<std::uint32_t>(1, budget_.maxHeavyGpuJobs))
        return {false, false, true, "resource.single-heavy-job"};
    return {true, totalRam.value() > budget_.ramSoftBytes, false, {}};
}
Result<void> ResourceGovernor::reserve(const ResourceRequest& r) {
    auto d = admit(r);
    if (!d.admit)
        return error(ErrorCode::BudgetExceeded, d.reason);
    reservations_.emplace(r.id, r);
    return {};
}
void ResourceGovernor::release(JobId id) {
    reservations_.erase(id);
}
} // namespace drawborad
