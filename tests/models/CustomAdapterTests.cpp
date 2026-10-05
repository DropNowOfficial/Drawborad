#include "Test.h"
#include "models/AdapterRegistry.h"
using namespace drawborad;
TEST("M05 unknown adapter is rejected and CPU fallback is explicit"){AdapterRegistry r;CHECK(!r.resolve("unknown.task"));ModelManifest m;m.taskId="sam21.image.v1";m.provider="cuda";auto p=probeModel(m,DevicePreference::CPU);CHECK(p);CHECK(p.value().supported);CHECK(p.value().reason=="cpu-fallback-explicit");}
