#include "Test.h"
#include "models/AdapterRegistry.h"
using namespace drawborad;

namespace {
ModelManifest canonical(std::string provider="cpu") {
    AdapterRegistry r;
    auto a=r.resolve("sam21.image.v1");
    if(!a) throw std::runtime_error("missing adapter");
    ModelManifest m;
    m.taskId="sam21.image.v1";
    m.abi=a.value().abi;
    m.provider=std::move(provider);
    m.inputs=a.value().inputs;
    m.outputs=a.value().outputs;
    m.ramBytes=2*GiB;
    m.gpuBytes=2*GiB;
    return m;
}
}

TEST("M05 unknown adapter is rejected and CPU fallback is explicit"){
    AdapterRegistry r;
    CHECK(!r.resolve("unknown.task"));
    auto m=canonical("cuda");
    auto p=probeModel(m,DevicePreference::CPU);
    CHECK(p);
    CHECK(p.value().supported);
    CHECK(p.value().reason=="cpu-fallback-explicit");
}

TEST("M05 incompatible declared image tensor is not qualified"){
    auto m=canonical();
    m.inputs[0].dtype="float16";
    auto p=probeModel(m,DevicePreference::CPU);
    CHECK(p);
    CHECK(!p.value().supported);
}

TEST("M05 missing tensor contract and shape bombs are rejected"){
    auto missing=canonical();
    missing.inputs.clear();
    auto a=probeModel(missing,DevicePreference::CPU);
    CHECK(a);
    CHECK(!a.value().supported);
    CHECK(a.value().reason=="tensor-contract-missing");

    auto bomb=canonical();
    bomb.inputs[1].shape={1,1000000,2};
    auto b=probeModel(bomb,DevicePreference::CPU);
    CHECK(b);
    CHECK(!b.value().supported);
    CHECK(b.value().reason=="input-contract-mismatch");
}

TEST("M05 incompatible ABI and resource budget are rejected"){
    auto abi=canonical();
    abi.abi="wrong";
    auto a=probeModel(abi,DevicePreference::CPU);
    CHECK(a);
    CHECK(!a.value().supported);
    CHECK(a.value().reason=="abi-mismatch");

    auto huge=canonical();
    huge.gpuBytes=7*GiB;
    auto b=probeModel(huge,DevicePreference::CPU);
    CHECK(b);
    CHECK(!b.value().supported);
    CHECK(b.value().reason=="gpu-budget-exceeded");
}

TEST("M05 CUDA request stays blocked until CUDA runtime is qualified"){
    auto m=canonical("cuda");
    auto p=probeModel(m,DevicePreference::CUDA);
    CHECK(p);
    CHECK(!p.value().supported);
    CHECK(p.value().reason=="cuda-runtime-qualification-required");
}

TEST("M05 SAM logical adapter exposes image prompts mask and int64 labels"){
    AdapterRegistry r;
    auto a=r.resolve("sam21.image.v1");
    CHECK(a);
    CHECK(a.value().inputs.size()==5);
    CHECK(a.value().inputs[0].name=="image");
    CHECK(a.value().inputs[1].name=="point_coords");
    CHECK(a.value().inputs[2].name=="point_labels");
    CHECK(a.value().inputs[2].dtype=="int64");
    CHECK(a.value().inputs[3].name=="mask_input");
    CHECK(a.value().inputs[4].name=="has_mask_input");
    CHECK(a.value().outputs.size()==2);
}
