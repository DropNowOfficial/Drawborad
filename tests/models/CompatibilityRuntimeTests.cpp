#include "Test.h"
#include "models/AdapterRegistry.h"
#include <cstdlib>
using namespace drawborad;

TEST("M05 resolved compatible model must pass an isolated runtime probe"){
    const char* encoder=std::getenv("DRAWBORAD_TEST_PROBE_ENCODER");
    const char* decoder=std::getenv("DRAWBORAD_TEST_PROBE_DECODER");
    CHECK(encoder!=nullptr);
    CHECK(decoder!=nullptr);
    AdapterRegistry registry;
    auto adapter=registry.resolve("sam21.image.v1");
    CHECK(adapter);
    ModelManifest manifest;
    manifest.taskId="sam21.image.v1";
    manifest.abi=adapter.value().abi;
    manifest.provider="cpu";
    manifest.inputs=adapter.value().inputs;
    manifest.outputs=adapter.value().outputs;
    manifest.resolvedWeights=encoder;
    manifest.resolvedDecoderWeights=decoder;
    manifest.sha256.bytes[0]=1;
    manifest.decoderSha256.bytes[0]=2;
    manifest.ramBytes=512*1024*1024;
    manifest.gpuBytes=0;
    auto report=probeModel(manifest,DevicePreference::CPU);
    CHECK(report);
    CHECK(report.value().supported);
    CHECK(report.value().reason=="runtime-probe-passed");
    CHECK(report.value().peakRam>0);
}
