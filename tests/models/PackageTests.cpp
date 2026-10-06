#include "Test.h"
#include "models/ModelManifest.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;
namespace {std::filesystem::path pkg(bool bad){auto d=std::filesystem::temp_directory_path()/(bad?"drawborad-model-bad":"drawborad-model-ok");std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{1,2,3,4};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),4);auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<(bad?std::string(64,'0'):h)<<"\",\"license\":\"test\"}";return d;}}
TEST("M01 valid package is checked by digest and untrusted install needs consent"){auto d=pkg(false);auto m=validatePackage(d);CHECK(m);ModelRegistry r;CHECK(!r.install(d,TrustDecision::TrustedSignature));auto id=r.install(d,TrustDecision::UserAcceptedUntrusted);CHECK(id);std::filesystem::remove_all(d);}
TEST("M01 tampered package is rejected"){auto d=pkg(true);auto m=validatePackage(d);CHECK(!m);CHECK(m.error().code==ErrorCode::DigestMismatch);std::filesystem::remove_all(d);}
TEST("M01 manifest external URL is rejected even when weights are local"){auto d=std::filesystem::temp_directory_path()/"drawborad-model-url";std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{1,2,3,4};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),4);auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<h<<"\",\"license\":\"test\",\"externalData\":\"https://example.invalid/x.bin\"}";f.close();auto r=validatePackage(d);CHECK(!r);std::filesystem::remove_all(d);}
TEST("M01 manifest schema and provider are allow-listed"){auto make=[](std::string name,int schema,std::string provider){auto d=std::filesystem::temp_directory_path()/name;std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{1,2,3,4};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),4);auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":"<<schema<<",\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<h<<"\",\"license\":\"test\",\"provider\":\""<<provider<<"\"}";f.close();return d;};auto badSchema=make("drawborad-model-schema",2,"cpu");CHECK(!validatePackage(badSchema));std::filesystem::remove_all(badSchema);auto badProvider=make("drawborad-model-provider",1,"remote");CHECK(!validatePackage(badProvider));std::filesystem::remove_all(badProvider);}

TEST("M01 two-stage SAM package verifies decoder digest independently"){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-two-stage";
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    Bytes encoder{1,2,3,4},decoder{5,6,7,8};
    std::ofstream(d/"encoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
    std::ofstream(d/"decoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));
    std::ofstream f(d/"manifest.json");
    f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\","
       "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
       "\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<sha256(decoder).hex()<<"\","
       "\"license\":\"test\",\"provider\":\"cpu\"}";
    f.close();
    auto ok=validatePackage(d);
    CHECK(ok);
    std::ofstream(d/"decoder.onnx",std::ios::binary|std::ios::trunc)<<"tampered";
    auto bad=validatePackage(d);
    CHECK(!bad);
    CHECK(bad.error().code==ErrorCode::DigestMismatch);
    std::filesystem::remove_all(d);
}
