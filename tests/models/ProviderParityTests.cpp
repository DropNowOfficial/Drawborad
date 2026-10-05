#include "Test.h"
#include "models/AdapterRegistry.h"
using namespace drawborad;
TEST("M02 runtime absence is explicit and never reported as qualified"){ModelManifest m;m.taskId="sam21.image.v1";OrtSession s(m);InferenceRequest q;q.modelId=Id::fromNumber(1);if(!s.runtimeAvailable()){auto r=s.run(q);CHECK(!r);CHECK(r.error().code==ErrorCode::UnsupportedFeature);}}
