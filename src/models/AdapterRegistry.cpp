#include "AdapterRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace drawborad {
namespace {
ModelReadinessState readinessFailureState(const Error& failure) {
    const auto& key = failure.messageKey;
    if (key == "model.package-dir")
        return ModelReadinessState::MissingModel;
    if (key == "model.onnxruntime-unavailable")
        return ModelReadinessState::RuntimeUnavailable;
    if (key == "model.provider" || key == "model.cuda-not-declared" ||
        key == "model.cpu-provider-unavailable" ||
        key == "model.cuda-provider-unavailable" || key == "model.cuda-provider-load-failed")
        return ModelReadinessState::ProviderUnavailable;
    if (key.starts_with("model.manifest") || key == "model.schema" ||
        key == "model.decoder-manifest-fields" || key == "model.sam-decoder-required" ||
        failure.code == ErrorCode::MissingDigest)
        return ModelReadinessState::InvalidManifest;
    if (failure.code == ErrorCode::DigestMismatch || failure.code == ErrorCode::PathEscape ||
        failure.code == ErrorCode::IoError || key == "model.executable-content")
        return ModelReadinessState::ModelIntegrityFailed;
    return ModelReadinessState::IncompatibleModel;
}

InferenceRequest smokeRequest(const ModelManifest& manifest, DevicePreference device) {
    InferenceRequest request;
    std::copy_n(manifest.sha256.bytes.begin(), 16, request.modelId.bytes.begin());
    if (request.modelId.empty())
        request.modelId = Id::fromNumber(1);
    request.device = device;
    const std::int64_t positive = 1;
    Bytes labels(sizeof(positive));
    std::memcpy(labels.data(), &positive, sizeof(positive));
    request.tensorRefs = {
        Bytes(3 * 1024 * 1024 * sizeof(float), 0),
        Bytes(2 * sizeof(float), 0),
        std::move(labels),
        Bytes(256 * 256 * sizeof(float), 0),
        Bytes(sizeof(float), 0)
    };
    return request;
}
}

std::string_view modelReadinessStateName(ModelReadinessState state) {
    switch (state) {
    case ModelReadinessState::MissingModel: return "MissingModel";
    case ModelReadinessState::InvalidManifest: return "InvalidManifest";
    case ModelReadinessState::ModelIntegrityFailed: return "ModelIntegrityFailed";
    case ModelReadinessState::RuntimeUnavailable: return "RuntimeUnavailable";
    case ModelReadinessState::ProviderUnavailable: return "ProviderUnavailable";
    case ModelReadinessState::IncompatibleModel: return "IncompatibleModel";
    case ModelReadinessState::Ready: return "Ready";
    }
    return "IncompatibleModel";
}

Result<ModelReadinessReport> preflightModelPackage(
    const PackagePath& package, DevicePreference device, std::stop_token stop) {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    ModelReadinessReport report;
    report.device = device;
    const auto failed = [&](const Error& failure) -> Result<ModelReadinessReport> {
        if (failure.code == ErrorCode::Cancelled)
            return failure;
        report.state = readinessFailureState(failure);
        report.messageKey = failure.messageKey;
        report.diagnostic = failure.diagnosticId;
        return report;
    };
    if (package.empty())
        return failed(error(ErrorCode::InvalidInput, "model.package-dir"));
    auto verified = validatePackage(package);
    if (!verified)
        return failed(verified.error());
    report.manifest = std::move(verified.value());
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    if (device == DevicePreference::CUDA && report.manifest->provider != "cuda")
        return failed(error(ErrorCode::UnsupportedAdapter, "model.cuda-not-declared"));
    OrtSession session(*report.manifest);
    if (!session.runtimeAvailable())
        return failed(error(ErrorCode::UnsupportedFeature, "model.onnxruntime-unavailable"));

    // Fresh session + owned image bytes force both encoder and decoder to run.
    // Passing this input qualifies the local interface, not segmentation quality.
    report.probeKind = "deterministic-smoke";
    auto inference = session.run(smokeRequest(*report.manifest, device), stop);
    if (!inference)
        return failed(inference.error());
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    const auto& tensors = inference.value().tensorRefs;
    constexpr std::size_t maskBytes = 256 * 256 * sizeof(float);
    if (tensors.size() != 2 || tensors[0].empty() || tensors[0].size() % maskBytes ||
        tensors[1].size() != tensors[0].size() / maskBytes * sizeof(float))
        return failed(error(ErrorCode::CorruptData, "model.sam-output-layout"));
    report.state = ModelReadinessState::Ready;
    report.messageKey = "model.ready";
    report.elapsedUs = inference.value().elapsedUs;
    report.observedTensorBytes = inference.value().peakBytes;
    return report;
}

Result<AdapterInfo> AdapterRegistry::resolve(std::string_view task) const {
    if (task != "sam21.image.v1")
        return error(ErrorCode::UnsupportedAdapter, "adapter.unknown");
    AdapterInfo a;
    a.id = "sam21.image.v1";
    a.abi = "sam21.image.v1";
    a.inputs = {
        {"image","float32",{1,3,1024,1024},16*1024*1024},
        {"point_coords","float32",{1,-1,2},4096},
        {"point_labels","int64",{1,-1},4096},
        {"mask_input","float32",{1,1,256,256},256*256*4},
        {"has_mask_input","float32",{1},4}
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

    if(device==DevicePreference::CUDA && m.provider!="cuda")
        return CompatibilityReport{false,"cuda-not-declared",0,0,device};
    if(device==DevicePreference::CUDA &&
       (m.resolvedWeights.empty() || m.resolvedDecoderWeights.empty()))
        return CompatibilityReport{false,"cuda-runtime-qualification-required",
                                   m.ramBytes,m.gpuBytes,device};
    if(device==DevicePreference::CPU && m.provider=="cuda" &&
       (m.resolvedWeights.empty() || m.resolvedDecoderWeights.empty()))
        return CompatibilityReport{true,"cpu-fallback-explicit",m.ramBytes,0,device};

    if(!m.resolvedWeights.empty() && !m.resolvedDecoderWeights.empty()) {
        auto floats=[](std::size_t count) {
            Bytes bytes(count*sizeof(float),0);
            return bytes;
        };
        auto int64s=[](std::initializer_list<std::int64_t> values) {
            Bytes bytes(values.size()*sizeof(std::int64_t));
            if(!bytes.empty())
                std::memcpy(bytes.data(),values.begin(),bytes.size());
            return bytes;
        };

        InferenceRequest request;
        std::copy_n(m.sha256.bytes.begin(),16,request.modelId.bytes.begin());
        if(request.modelId.empty())
            request.modelId=Id::fromNumber(1);
        request.device=device;
        request.tensorRefs={
            floats(3*1024*1024),
            floats(2),
            int64s({1}),
            floats(256*256),
            floats(1)
        };
        if(stop.stop_requested())
            return error(ErrorCode::Cancelled,"adapter.cancelled");
        OrtSession session(m);
        auto probe=session.run(request,stop);
        if(!probe)
            return CompatibilityReport{false,probe.error().messageKey,0,0,device};
        if(probe.value().tensorRefs.size()!=m.outputs.size())
            return CompatibilityReport{false,"runtime-output-count",0,0,device};
        return CompatibilityReport{true,"runtime-probe-passed",
                                   probe.value().peakBytes,0,device};
    }

    if(m.provider=="cuda")
        return CompatibilityReport{true,"cpu-fallback-explicit",m.ramBytes,0,device};
    return CompatibilityReport{true,"manifest-contract-valid",m.ramBytes,m.gpuBytes,device};
}
Result<CompatibilityReport> probeModel(const ModelRegistry& registry,ModelId id,
                                       DevicePreference device,std::stop_token stop){
    auto manifest=registry.manifest(id);
    if(!manifest)return manifest.error();
    return probeModel(manifest.value(),device,stop);
}

}
