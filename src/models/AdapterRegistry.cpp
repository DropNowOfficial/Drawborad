#include "AdapterRegistry.h"
#include <cmath>

namespace drawborad {
Result<AdapterInfo> AdapterRegistry::resolve(std::string_view task) const {
    if (task != "sam21.image.v1")
        return error(ErrorCode::UnsupportedAdapter, "adapter.unknown");
    AdapterInfo a;
    a.id = "sam21.image.v1";
    a.abi = "sam21.image.v1";
    a.inputs = {
        {"image","float32",{1,3,1024,1024},16*1024*1024},
        {"point_coords","float32",{1,-1,2},4096},
        {"point_labels","float32",{1,-1},2048}
    };
    a.outputs = {
        {"low_res_masks","float32",{1,-1,256,256},8*1024*1024},
        {"iou_scores","float32",{1,-1},64}
    };
    return a;
}

Result<CompatibilityReport> probeModel(const ModelManifest& m, DevicePreference device,
                                       std::stop_token stop) {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "adapter.cancelled");

    AdapterRegistry registry;
    auto adapter = registry.resolve(m.taskId);
    if (!adapter)
        return adapter.error();

    if (!m.abi.empty() && m.abi != adapter.value().abi)
        return CompatibilityReport{false,"abi-mismatch",0,0,device};
    if (m.inputs.empty() || m.outputs.empty())
        return CompatibilityReport{false,"tensor-contract-missing",0,0,device};
    if (m.inputs.size()!=adapter.value().inputs.size())
        return CompatibilityReport{false,"input-contract-mismatch",0,0,device};
    if (m.outputs.size()!=adapter.value().outputs.size())
        return CompatibilityReport{false,"output-contract-mismatch",0,0,device};

    auto compatible=[](const TensorSpec& got,const TensorSpec& expected) {
        if(got.name!=expected.name||got.dtype!=expected.dtype||
           got.shape.size()!=expected.shape.size()||got.byteLimit==0||
           got.byteLimit>expected.byteLimit)
            return false;
        for(std::size_t i=0;i<got.shape.size();++i){
            if(got.shape[i]==0||got.shape[i]<-1)
                return false;
            if(expected.shape[i]>=0){
                if(got.shape[i]!=expected.shape[i])
                    return false;
            } else if(got.shape[i]>4096) {
                return false;
            }
        }
        return true;
    };

    for(std::size_t i=0;i<m.inputs.size();++i)
        if(!compatible(m.inputs[i],adapter.value().inputs[i]))
            return CompatibilityReport{false,"input-contract-mismatch",0,0,device};
    for(std::size_t i=0;i<m.outputs.size();++i)
        if(!compatible(m.outputs[i],adapter.value().outputs[i]))
            return CompatibilityReport{false,"output-contract-mismatch",0,0,device};

    ResourceBudget budget;
    if(m.ramBytes>budget.ramReclaimBytes)
        return CompatibilityReport{false,"ram-budget-exceeded",m.ramBytes,m.gpuBytes,device};
    if(m.gpuBytes>budget.gpuTotalBytes)
        return CompatibilityReport{false,"gpu-budget-exceeded",m.ramBytes,m.gpuBytes,device};

    if(device==DevicePreference::CUDA){
        if(m.provider!="cuda")
            return CompatibilityReport{false,"cuda-not-declared",0,0,device};
        return CompatibilityReport{false,"cuda-runtime-qualification-required",
                                   m.ramBytes,m.gpuBytes,device};
    }
    if(m.provider=="cuda")
        return CompatibilityReport{true,"cpu-fallback-explicit",m.ramBytes,0,device};
    return CompatibilityReport{true,"manifest-contract-valid",m.ramBytes,m.gpuBytes,device};
}
}
