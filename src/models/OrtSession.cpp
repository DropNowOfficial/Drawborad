#include "ModelManifest.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace drawborad {
struct OrtSessionState {
    std::mutex mutex;
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Session> encoder, decoder;
    DevicePreference device{DevicePreference::CPU};
    Bytes image;
    std::vector<Ort::Value> features;
#endif
};

OrtSession::OrtSession(ModelManifest manifest)
    : manifest_(std::move(manifest)), state_(std::make_shared<OrtSessionState>()) {}

#ifdef DRAWBORAD_HAS_ONNXRUNTIME
namespace {

bool shapeMatches(std::span<const std::int64_t> expected,
                  std::span<const std::int64_t> actual) {
    if (expected.size() != actual.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] == 0 || expected[i] < -1 || actual[i] <= 0)
            return false;
        if (expected[i] != -1 && expected[i] != actual[i])
            return false;
    }
    return true;
}

Result<std::vector<std::int64_t>> resolveShape(std::vector<std::int64_t> shape,
                                               std::size_t elements) {
    std::size_t dynamic = shape.size();
    std::uint64_t known = 1;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] < -1)
            return error(ErrorCode::InvalidInput, "model.invalid-dimension");
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
        auto next = checkedMultiply(known, inferred);
        if (!next)
            return next.error();
        known = next.value();
    }
    if (known != elements)
        return error(ErrorCode::InvalidInput, "model.tensor-size");
    return shape;
}

Result<ONNXTensorElementDataType> elementType(std::string_view dtype) {
    if (dtype == "float32")
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    if (dtype == "int64")
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    return error(ErrorCode::UnsupportedFeature, "model.tensor-dtype");
}

std::size_t elementBytes(ONNXTensorElementDataType type) {
    switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
        return sizeof(float);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
        return sizeof(std::int64_t);
    default:
        return 0;
    }
}

Result<Ort::Value> inputTensor(const Ort::MemoryInfo& memoryInfo, const Bytes& bytes,
                               const TensorSpec& spec,
                               ONNXTensorElementDataType runtimeType,
                               std::vector<std::int64_t> runtimeShape) {
    auto wanted = elementType(spec.dtype);
    if (!wanted)
        return wanted.error();
    if (runtimeType != wanted.value())
        return error(ErrorCode::InvalidInput, "model.runtime-input-dtype");
    const auto width = elementBytes(wanted.value());
    if (!width || bytes.size() % width)
        return error(ErrorCode::InvalidInput, "model.input-byte-alignment");
    if (spec.byteLimit && bytes.size() > spec.byteLimit)
        return error(ErrorCode::BudgetExceeded, "model.input-byte-limit");

    auto shape = spec.shape.empty() ? runtimeShape : spec.shape;
    auto resolved = resolveShape(std::move(shape), bytes.size() / width);
    if (!resolved)
        return resolved.error();
    if (!shapeMatches(runtimeShape, resolved.value()))
        return error(ErrorCode::InvalidInput, "model.runtime-input-shape");

    return Ort::Value::CreateTensor(
        memoryInfo, const_cast<std::uint8_t*>(bytes.data()), bytes.size(),
        resolved.value().data(), resolved.value().size(), wanted.value());
}

Result<Bytes> copyFloatOutput(const Ort::Value& output, const TensorSpec* spec,
                              std::string_view name) {
    if (!output.IsTensor())
        return error(ErrorCode::UnsupportedFeature, "model.non-tensor-output");
    auto info = output.GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
        return Error{ErrorCode::UnsupportedFeature, "model.runtime-output-dtype",
                     std::string(name)};
    const auto count = info.GetElementCount();
    auto byteCount = checkedMultiply(std::uint64_t(count), sizeof(float));
    if (!byteCount)
        return byteCount.error();
    if (spec) {
        if (!spec->dtype.empty() && spec->dtype != "float32")
            return error(ErrorCode::UnsupportedFeature, "model.output-dtype");
        if (!spec->shape.empty() && !shapeMatches(spec->shape, info.GetShape()))
            return error(ErrorCode::CorruptData, "model.runtime-output-shape");
        if (spec->byteLimit && byteCount.value() > spec->byteLimit)
            return error(ErrorCode::BudgetExceeded, "model.output-byte-limit");
    }
    const auto* data = output.GetTensorData<float>();
    for (std::size_t i = 0; i < count; ++i)
        if (!std::isfinite(data[i]))
            return error(ErrorCode::CorruptData, "model.nonfinite-output");
    Bytes bytes(std::size_t(byteCount.value()));
    if (!bytes.empty())
        std::memcpy(bytes.data(), data, bytes.size());
    return bytes;
}

Result<std::uint64_t> tensorBytes(const Ort::Value& value) {
    if (!value.IsTensor())
        return error(ErrorCode::UnsupportedFeature, "model.non-tensor-output");
    auto info = value.GetTensorTypeAndShapeInfo();
    const auto width = elementBytes(info.GetElementType());
    if (!width)
        return error(ErrorCode::UnsupportedFeature, "model.tensor-dtype");
    return checkedMultiply(std::uint64_t(info.GetElementCount()), width);
}

Result<void> requireName(Ort::Session& session, bool input, std::size_t index,
                         std::string_view expected, Ort::AllocatorWithDefaultOptions& allocator) {
    auto name = input ? session.GetInputNameAllocated(index, allocator)
                      : session.GetOutputNameAllocated(index, allocator);
    if (!name || expected != name.get())
        return Error{ErrorCode::InvalidInput,
                     input ? "model.input-name" : "model.output-name",
                     name ? name.get() : ""};
    return {};
}

Result<void> configureProvider(Ort::SessionOptions& options,
                               const ModelManifest& manifest,
                               DevicePreference device) {
    const auto providers = Ort::GetAvailableProviders();
    const auto wanted = device == DevicePreference::CPU
        ? "CPUExecutionProvider" : "CUDAExecutionProvider";
    if (std::find(providers.begin(), providers.end(), wanted) == providers.end())
        return error(ErrorCode::UnsupportedFeature, device == DevicePreference::CPU
            ? "model.cpu-provider-unavailable" : "model.cuda-provider-unavailable");
    if (device == DevicePreference::CPU)
        return {};

    OrtCUDAProviderOptions cudaOptions{};
    cudaOptions.device_id = 0;
    cudaOptions.do_copy_in_default_stream = 1;
    cudaOptions.arena_extend_strategy = 1;
    if (manifest.gpuBytes)
        cudaOptions.gpu_mem_limit = std::size_t(
            std::min<std::uint64_t>(manifest.gpuBytes, 6 * GiB));
    try {
        options.AppendExecutionProvider_CUDA(cudaOptions);
    } catch (const Ort::Exception& ex) {
        return Error{ErrorCode::UnsupportedFeature, "model.cuda-provider-load-failed", ex.what()};
    }
    return {};
}

Result<InferenceOutput> runSingle(Ort::Env& env, const ModelManifest& manifest,
                                  const InferenceRequest& request, std::stop_token stop) {
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    auto provider = configureProvider(options, manifest, request.device);
    if (!provider)
        return provider.error();
    Ort::Session session(env, manifest.resolvedWeights.c_str(), options);
    Ort::AllocatorWithDefaultOptions allocator;

    const auto inputCount = session.GetInputCount();
    const auto outputCount = session.GetOutputCount();
    if (request.tensorRefs.size() != inputCount)
        return error(ErrorCode::InvalidInput, "model.input-count");
    if (!manifest.inputs.empty() && manifest.inputs.size() != inputCount)
        return error(ErrorCode::InvalidInput, "model.manifest-input-count");
    if (!manifest.outputs.empty() && manifest.outputs.size() != outputCount)
        return error(ErrorCode::InvalidInput, "model.manifest-output-count");

    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<std::string> inputNamesStorage;
    std::vector<const char*> inputNames;
    std::vector<Ort::Value> inputValues;
    inputNamesStorage.reserve(inputCount);
    inputNames.reserve(inputCount);
    inputValues.reserve(inputCount);
    std::uint64_t knownTensorBytes = 0;

    for (std::size_t i = 0; i < inputCount; ++i) {
        auto name = session.GetInputNameAllocated(i, allocator);
        inputNamesStorage.emplace_back(name.get());
        auto typeInfo = session.GetInputTypeInfo(i);
        auto info = typeInfo.GetTensorTypeAndShapeInfo();
        TensorSpec runtimeSpec;
        runtimeSpec.name = inputNamesStorage.back();
        runtimeSpec.dtype = "float32";
        runtimeSpec.shape = info.GetShape();
        const TensorSpec& spec = manifest.inputs.empty() ? runtimeSpec : manifest.inputs[i];
        if (!spec.name.empty() && spec.name != inputNamesStorage.back())
            return error(ErrorCode::InvalidInput, "model.input-name");
        auto value = inputTensor(memoryInfo, request.tensorRefs[i], spec,
                                 info.GetElementType(), info.GetShape());
        if (!value)
            return value.error();
        inputValues.push_back(std::move(value.value()));
        inputNames.push_back(inputNamesStorage.back().c_str());
        auto next = checkedAdd(knownTensorBytes, request.tensorRefs[i].size());
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
        if (!manifest.outputs.empty() && !manifest.outputs[i].name.empty() &&
            manifest.outputs[i].name != outputNamesStorage.back())
            return error(ErrorCode::InvalidInput, "model.output-name");
        outputNames.push_back(outputNamesStorage.back().c_str());
    }

    if (stop.stop_requested())
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
        const TensorSpec* spec = manifest.outputs.empty() ? nullptr : &manifest.outputs[i];
        auto bytes = copyFloatOutput(outputs[i], spec, outputNamesStorage[i]);
        if (!bytes)
            return bytes.error();
        auto next = checkedAdd(knownTensorBytes, bytes.value().size());
        if (!next)
            return next.error();
        knownTensorBytes = next.value();
        result.tensorRefs.push_back(std::move(bytes.value()));
    }
    result.peakBytes = knownTensorBytes;
    return result;
}

Result<InferenceOutput> runSam21(Ort::Env& env, const ModelManifest& manifest,
                                 const InferenceRequest& request, std::stop_token stop,
                                 OrtSessionState& state) {
    if (manifest.resolvedDecoderWeights.empty())
        return error(ErrorCode::InvalidInput, "model.sam-decoder-path");
    if (manifest.inputs.size() != 5 || manifest.outputs.size() != 2 ||
        request.tensorRefs.size() != 5)
        return error(ErrorCode::InvalidInput, "model.sam-logical-contract");

    if (!state.encoder || !state.decoder || state.device != request.device) {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        auto provider = configureProvider(options, manifest, request.device);
        if (!provider) return provider.error();
        auto encoder = std::make_unique<Ort::Session>(env, manifest.resolvedWeights.c_str(), options);
        auto decoder = std::make_unique<Ort::Session>(env, manifest.resolvedDecoderWeights.c_str(), options);
        state.encoder = std::move(encoder);
        state.decoder = std::move(decoder);
        state.device = request.device;
        state.features.clear();
        state.image.clear();
    }
    auto& encoder = *state.encoder;
    auto& decoder = *state.decoder;
    Ort::AllocatorWithDefaultOptions allocator;
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    if (encoder.GetInputCount() != 1 || encoder.GetOutputCount() != 3)
        return error(ErrorCode::InvalidInput, "model.sam-encoder-contract");
    auto imageName = requireName(encoder, true, 0, "image", allocator);
    if (!imageName)
        return imageName.error();
    constexpr std::array<std::string_view, 3> encoderOutputNames{
        "image_embed", "high_res_0", "high_res_1"};
    for (std::size_t i = 0; i < encoderOutputNames.size(); ++i) {
        auto ok = requireName(encoder, false, i, encoderOutputNames[i], allocator);
        if (!ok)
            return ok.error();
    }

    auto imageTypeInfo = encoder.GetInputTypeInfo(0);
    auto imageInfo = imageTypeInfo.GetTensorTypeAndShapeInfo();
    auto imageValue = inputTensor(memoryInfo, request.tensorRefs[0], manifest.inputs[0],
                                  imageInfo.GetElementType(), imageInfo.GetShape());
    if (!imageValue)
        return imageValue.error();
    const char* encoderInputNames[] = {"image"};
    const char* encoderOutputs[] = {"image_embed", "high_res_0", "high_res_1"};

    std::uint64_t knownTensorBytes = 0;
    for (const auto& bytes : request.tensorRefs) {
        auto next = checkedAdd(knownTensorBytes, bytes.size());
        if (!next)
            return next.error();
        knownTensorBytes = next.value();
    }

    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    const auto begin = std::chrono::steady_clock::now();
    Ort::RunOptions encoderOptions;
    std::stop_callback cancelEncoder(stop, [&] { encoderOptions.SetTerminate(); });
    if (state.features.empty() || state.image != request.tensorRefs[0]) {
        // Compare owned bytes, not a caller-supplied revision/hash. A changed
        // image must never reuse the previous image's encoded features.
        Bytes nextImage = request.tensorRefs[0];
        auto features = encoder.Run(encoderOptions, encoderInputNames, &imageValue.value(), 1,
                                    encoderOutputs, 3);
        // Validate once before caching, including features a decoder may ignore.
        // Reused image features do not require rescanning for every prompt.
        for (const auto& feature : features) {
            if (!feature.IsTensor())
                return error(ErrorCode::UnsupportedFeature, "model.non-tensor-output");
            const auto info = feature.GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return error(ErrorCode::UnsupportedFeature, "model.sam-feature-dtype");
            const auto* data = feature.GetTensorData<float>();
            const auto count = info.GetElementCount();
            for (std::size_t i = 0; i < count; ++i)
                if (!std::isfinite(data[i]))
                    return error(ErrorCode::CorruptData, "model.nonfinite-output");
        }
        state.features = std::move(features);
        state.image.swap(nextImage);
    }
    for (const auto& feature : state.features) {
        auto size = tensorBytes(feature);
        if (!size)
            return size.error();
        auto next = checkedAdd(knownTensorBytes, size.value());
        if (!next)
            return next.error();
        knownTensorBytes = next.value();
    }

    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    constexpr std::array<std::string_view, 7> decoderInputNames{
        "image_embed", "high_res_0", "high_res_1", "point_coords",
        "point_labels", "mask_input", "has_mask_input"};
    constexpr std::array<std::string_view, 2> decoderOutputNames{
        "low_res_masks", "iou_scores"};
    if (decoder.GetInputCount() != decoderInputNames.size() ||
        decoder.GetOutputCount() != decoderOutputNames.size())
        return error(ErrorCode::InvalidInput, "model.sam-decoder-contract");
    for (std::size_t i = 0; i < decoderInputNames.size(); ++i) {
        auto ok = requireName(decoder, true, i, decoderInputNames[i], allocator);
        if (!ok)
            return ok.error();
    }
    for (std::size_t i = 0; i < decoderOutputNames.size(); ++i) {
        auto ok = requireName(decoder, false, i, decoderOutputNames[i], allocator);
        if (!ok)
            return ok.error();
    }

    std::vector<Ort::Value> decoderInputs;
    decoderInputs.reserve(7);
    for (const auto& feature : state.features) {
        const auto info = feature.GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            return error(ErrorCode::UnsupportedFeature, "model.sam-feature-dtype");
        const auto shape = info.GetShape();
        const auto index = decoderInputs.size();
        const auto encoderType = encoder.GetOutputTypeInfo(index);
        const auto encoderInfo = encoderType.GetTensorTypeAndShapeInfo();
        const auto decoderType = decoder.GetInputTypeInfo(index);
        const auto decoderInfo = decoderType.GetTensorTypeAndShapeInfo();
        if (encoderInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            decoderInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            return error(ErrorCode::UnsupportedFeature, "model.sam-feature-dtype");
        if (!shapeMatches(encoderInfo.GetShape(), shape) ||
            !shapeMatches(decoderInfo.GetShape(), shape))
            return error(ErrorCode::InvalidInput, "model.sam-feature-shape");
        decoderInputs.push_back(Ort::Value::CreateTensor<float>(memoryInfo,
            const_cast<float*>(feature.GetTensorData<float>()), info.GetElementCount(),
            shape.data(), shape.size()));
    }
    for (std::size_t logical = 1; logical < 5; ++logical) {
        const auto decoderIndex = logical + 2;
        auto typeInfo = decoder.GetInputTypeInfo(decoderIndex);
        auto info = typeInfo.GetTensorTypeAndShapeInfo();
        auto value = inputTensor(memoryInfo, request.tensorRefs[logical],
                                 manifest.inputs[logical], info.GetElementType(),
                                 info.GetShape());
        if (!value)
            return value.error();
        decoderInputs.push_back(std::move(value.value()));
    }

    std::array<const char*, 7> decoderNames{};
    for (std::size_t i = 0; i < decoderNames.size(); ++i)
        decoderNames[i] = decoderInputNames[i].data();
    std::array<const char*, 2> outputNames{};
    for (std::size_t i = 0; i < outputNames.size(); ++i)
        outputNames[i] = decoderOutputNames[i].data();

    Ort::RunOptions decoderOptions;
    std::stop_callback cancelDecoder(stop, [&] { decoderOptions.SetTerminate(); });
    auto outputs = decoder.Run(decoderOptions, decoderNames.data(), decoderInputs.data(),
                               decoderInputs.size(), outputNames.data(), outputNames.size());
    const auto end = std::chrono::steady_clock::now();

    InferenceOutput result;
    result.elapsedUs = std::uint64_t(
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
    result.tensorRefs.reserve(outputs.size());
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        auto bytes = copyFloatOutput(outputs[i], &manifest.outputs[i], decoderOutputNames[i]);
        if (!bytes)
            return bytes.error();
        auto next = checkedAdd(knownTensorBytes, bytes.value().size());
        if (!next)
            return next.error();
        knownTensorBytes = next.value();
        result.tensorRefs.push_back(std::move(bytes.value()));
    }
    result.peakBytes = knownTensorBytes;
    return result;
}

} // namespace
#endif

bool OrtSession::runtimeAvailable() const {
#ifdef DRAWBORAD_HAS_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

Result<InferenceOutput> OrtSession::run(const InferenceRequest& request,
                                        std::stop_token stop) const {
    if (stop.stop_requested())
        return error(ErrorCode::Cancelled, "model.cancelled");
    if (request.modelId.empty())
        return error(ErrorCode::InvalidInput, "model.id");
    if (manifest_.taskId != "sam21.image.v1")
        return error(ErrorCode::UnsupportedAdapter, "model.task");
    if (!runtimeAvailable())
        return error(ErrorCode::UnsupportedFeature, "model.onnxruntime-unavailable");
    if (manifest_.resolvedWeights.empty())
        return error(ErrorCode::InvalidInput, "model.unverified-runtime-path");
    if (request.device == DevicePreference::CUDA && manifest_.provider != "cuda")
        return error(ErrorCode::UnsupportedAdapter, "model.cuda-not-declared");

#ifdef DRAWBORAD_HAS_ONNXRUNTIME
    try {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "drawborad");
        std::lock_guard lock(state_->mutex);
        if (!manifest_.resolvedDecoderWeights.empty() || manifest_.inputs.size() == 5)
            return runSam21(env, manifest_, request, stop, *state_);
        return runSingle(env, manifest_, request, stop);
    } catch (const Ort::Exception& ex) {
        if (stop.stop_requested())
            return error(ErrorCode::Cancelled, "model.cancelled");
        if (request.device == DevicePreference::CUDA && ex.GetOrtErrorCode() == ORT_EP_FAIL)
            return Error{ErrorCode::UnsupportedFeature, "model.cuda-provider-load-failed", ex.what()};
        return Error{ErrorCode::CorruptData, "model.onnxruntime-error", ex.what()};
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "model.runtime-allocation");
    }
#else
    (void)request;
    return error(ErrorCode::UnsupportedFeature, "model.onnxruntime-unavailable");
#endif
}
}
