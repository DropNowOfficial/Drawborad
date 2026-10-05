#include "Test.h"
#include "models/ModelManifest.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;
namespace {std::filesystem::path pkg(bool bad){auto d=std::filesystem::temp_directory_path()/(bad?"drawborad-model-bad":"drawborad-model-ok");std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{1,2,3,4};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),4);auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<(bad?std::string(64,'0'):h)<<"\",\"license\":\"test\"}";return d;}}
TEST("M01 valid package is checked by digest and untrusted install needs consent"){auto d=pkg(false);auto m=validatePackage(d);CHECK(m);ModelRegistry r;CHECK(!r.install(d,TrustDecision::TrustedSignature));auto id=r.install(d,TrustDecision::UserAcceptedUntrusted);CHECK(id);std::filesystem::remove_all(d);}
TEST("M01 tampered package is rejected"){auto d=pkg(true);auto m=validatePackage(d);CHECK(!m);CHECK(m.error().code==ErrorCode::DigestMismatch);std::filesystem::remove_all(d);}
