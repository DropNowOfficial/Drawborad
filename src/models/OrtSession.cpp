#include "ModelManifest.h"
#include <chrono>
#include <cstring>
#include <limits>
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace drawborad {
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
namespace {
Result<std::vector<std::int64_t>> resolveShape(std::vector<std::int64_t> shape,
                                               std::size_t elements) {
    std::size_t dynamic = shape.size();
    std::uint64_t known = 1;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] < 0) {
            if (dynamic != shape.size())
                return error(ErrorCode::UnsupportedFeature, "model.multiple-dynamic-dims");
            dynamic = i;
            continue;
        }
        if (shape[i] == 0)
            return error(ErrorCode::InvalidInput, "model.zero-dimension");
        auto next = checkedMultiply(known, std::uint64_t(shape[i]));
        if (!next)
            return next.error();
        known = next.value();
    }
    if (dynamic != shape.size()) {
        if (!known || elements % known)
            return error(ErrorCode::InvalidInput, "model.dynamic-shape-size");
        const auto inferred = elements / known;
        if (!inferred || inferred > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
            return error(ErrorCode::BudgetExceeded, "model.dynamic-shape-limit");
        shape[dynamic] = std::int64_t(inferred);
        known *= inferred;
    }
    if (known != elements)
        return error(ErrorCode::InvalidInput, "model.tensor-size");
    return shape;
}
}
#endif

bool OrtSession::runtimeAvailable() const {
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

Result<InferenceOutput> OrtSession::run(const InferenceRequest& q, std::stop_token st) const {
    if (st.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    if (q.modelId.empty())
        return error(ErrorCode::InvalidInput, "model.id");
    if (manifest_.taskId != "sam21.image.v1")
        return error(ErrorCode::UnsupportedAdapter, "model.task");
    if (!runtimeAvailable())
        return error(ErrorCode::UnsupportedFeature, "model.onnxruntime-unavailable");
    if (manifest_.resolvedWeights.empty())
        return error(ErrorCode::InvalidInput, "model.unverified-runtime-path");
    if (q.device == DevicePreference::CUDA)
        return error(ErrorCode::UnsupportedFeature, "model.cuda-provider-not-qualified");

#ifdef DRAWBORAD_HAS_ONNXRUNTIME
    try {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "drawborad");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);

        Ort::Session session(env, manifest_.resolvedWeights.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;

        const auto inputCount = session.GetInputCount();
        const auto outputCount = session.GetOutputCount();
        if (q.tensorRefs.size() != inputCount)
            return error(ErrorCode::InvalidInput, "model.input-count");
        if (!manifest_.inputs.empty() && manifest_.inputs.size() != inputCount)
            return error(ErrorCode::InvalidInput, "model.manifest-input-count");
        if (!manifest_.outputs.empty() && manifest_.outputs.size() != outputCount)
            return error(ErrorCode::InvalidInput, "model.manifest-output-count");

        std::vector<std::string> inputNamesStorage;
        std::vector<const char*> inputNames;
        std::vector<std::vector<float>> inputBuffers;
        std::vector<std::vector<std::int64_t>> inputShapes;
        std::vector<Ort::Value> inputValues;
        inputNamesStorage.reserve(inputCount);
        inputNames.reserve(inputCount);
        inputBuffers.reserve(inputCount);
        inputShapes.reserve(inputCount);
        inputValues.reserve(inputCount);
        std::uint64_t knownTensorBytes = 0;

        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        for (std::size_t i = 0; i < inputCount; ++i) {
            auto name = session.GetInputNameAllocated(i, allocator);
            inputNamesStorage.emplace_back(name.get());
            if (!manifest_.inputs.empty()) {
                const auto& spec = manifest_.inputs[i];
                if (!spec.name.empty() && spec.name != inputNamesStorage.back())
                    return error(ErrorCode::InvalidInput, "model.input-name");
                if (!spec.dtype.empty() && spec.dtype != "float32")
                    return error(ErrorCode::UnsupportedFeature, "model.input-dtype");
                if (spec.byteLimit && q.tensorRefs[i].size() > spec.byteLimit)
                    return error(ErrorCode::BudgetExceeded, "model.input-byte-limit");
            }
            if (q.tensorRefs[i].size() % sizeof(float))
                return error(ErrorCode::InvalidInput, "model.input-byte-alignment");
            const auto elements = q.tensorRefs[i].size() / sizeof(float);
            auto type = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            if (type.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return error(ErrorCode::UnsupportedFeature, "model.runtime-input-dtype");
            auto shape = type.GetShape();
            if (!manifest_.inputs.empty() && !manifest_.inputs[i].shape.empty())
                shape = manifest_.inputs[i].shape;
            auto resolved = resolveShape(std::move(shape), elements);
            if (!resolved)
                return resolved.error();
            inputShapes.push_back(std::move(resolved.value()));
            inputBuffers.emplace_back(elements);
            if (!q.tensorRefs[i].empty())
                std::memcpy(inputBuffers.back().data(), q.tensorRefs[i].data(), q.tensorRefs[i].size());
            inputValues.push_back(Ort::Value::CreateTensor<float>(
                memoryInfo, inputBuffers.back().data(), inputBuffers.back().size(),
                inputShapes.back().data(), inputShapes.back().size()));
            inputNames.push_back(inputNamesStorage.back().c_str());
            auto next = checkedAdd(knownTensorBytes, q.tensorRefs[i].size());
            if (!next)
                return next.error();
            knownTensorBytes = next.value();
        }

        std::vector<std::string> outputNamesStorage;
        std::vector<const char*> outputNames;
        outputNamesStorage.reserve(outputCount);
        outputNames.reserve(outputCount);
        for (std::size_t i = 0; i < outputCount; ++i) {
            auto name = session.GetOutputNameAllocated(i, allocator);
            outputNamesStorage.emplace_back(name.get());
            if (!manifest_.outputs.empty()) {
                const auto& spec = manifest_.outputs[i];
                if (!spec.name.empty() && spec.name != outputNamesStorage.back())
                    return error(ErrorCode::InvalidInput, "model.output-name");
                if (!spec.dtype.empty() && spec.dtype != "float32")
                    return error(ErrorCode::UnsupportedFeature, "model.output-dtype");
            }
            outputNames.push_back(outputNamesStorage.back().c_str());
        }

        if (st.stop_requested())
            return error(ErrorCode::Cancelled, "model.cancelled");

        const auto begin = std::chrono::steady_clock::now();
        Ort::RunOptions runOptions;
        auto outputs = session.Run(runOptions, inputNames.data(), inputValues.data(),
                                   inputValues.size(), outputNames.data(), outputNames.size());
        const auto end = std::chrono::steady_clock::now();

        InferenceOutput result;
        result.elapsedUs = std::uint64_t(
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        result.tensorRefs.reserve(outputs.size());
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (!outputs[i].IsTensor())
                return error(ErrorCode::UnsupportedFeature, "model.non-tensor-output");
            auto info = outputs[i].GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return error(ErrorCode::UnsupportedFeature, "model.runtime-output-dtype");
            const auto count = info.GetElementCount();
            auto bytesResult = checkedMultiply(count, sizeof(float));
            if (!bytesResult)
                return bytesResult.error();
            if (!manifest_.outputs.empty() && manifest_.outputs[i].byteLimit &&
                bytesResult.value() > manifest_.outputs[i].byteLimit)
                return error(ErrorCode::BudgetExceeded, "model.output-byte-limit");
            Bytes bytes(std::size_t(bytesResult.value()));
            if (!bytes.empty())
                std::memcpy(bytes.data(), outputs[i].GetTensorData<float>(), bytes.size());
            auto next = checkedAdd(knownTensorBytes, bytes.size());
            if (!next)
                return next.error();
            knownTensorBytes = next.value();
            result.tensorRefs.push_back(std::move(bytes));
        }
        result.peakBytes = knownTensorBytes;
        return result;
    } catch (const Ort::Exception& ex) {
        return Error{ErrorCode::CorruptData, "model.onnxruntime-error", ex.what()};
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "model.runtime-allocation");
    }
#else
    (void)q;
    return error(ErrorCode::UnsupportedFeature, "model.onnxruntime-unavailable");
#endif
}
}
