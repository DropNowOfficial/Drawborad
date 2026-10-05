#include "Test.h"
#include "models/AdapterRegistry.h"
using namespace drawborad;
TEST("M05 unknown adapter is rejected and CPU fallback is explicit"){AdapterRegistry r;CHECK(!r.resolve("unknown.task"));ModelManifest m;m.taskId="sam21.image.v1";m.provider="cuda";auto p=probeModel(m,DevicePreference::CPU);CHECK(p);CHECK(p.value().supported);CHECK(p.value().reason=="cpu-fallback-explicit");}
TEST("M05 incompatible declared image tensor is not qualified"){ModelManifest m;m.taskId="sam21.image.v1";m.provider="cpu";m.inputs={{"image","float16",{1,3,1024,1024},16*1024*1024}};auto p=probeModel(m,DevicePreference::CPU);CHECK(p);CHECK(!p.value().supported);}
