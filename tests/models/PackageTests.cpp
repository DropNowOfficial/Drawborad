#include "Test.h"
#include "models/ModelManifest.h"
#include <filesystem>
#include <fstream>
using namespace drawborad;
namespace {std::filesystem::path pkg(bool bad){auto d=std::filesystem::temp_directory_path()/(bad?"drawborad-model-bad":"drawborad-model-ok");std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{0x3a,0x00},decoder{0x3a,0x00};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),std::streamsize(w.size()));std::ofstream(d/"decoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));auto h=sha256(w).hex(),dh=sha256(decoder).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<(bad?std::string(64,'0'):h)<<"\",\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<dh<<"\",\"license\":\"test\"}";return d;}}
TEST("M01 valid package is checked by digest and untrusted install needs consent"){auto d=pkg(false);auto m=validatePackage(d);CHECK(m);ModelRegistry r;CHECK(!r.install(d,TrustDecision::TrustedSignature));auto id=r.install(d,TrustDecision::UserAcceptedUntrusted);CHECK(id);std::filesystem::remove_all(d);}
TEST("M01 tampered package is rejected"){auto d=pkg(true);auto m=validatePackage(d);CHECK(!m);CHECK(m.error().code==ErrorCode::DigestMismatch);std::filesystem::remove_all(d);}
TEST("M01 manifest external URL is rejected even when weights are local"){auto d=std::filesystem::temp_directory_path()/"drawborad-model-url";std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{0x3a,0x00};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),std::streamsize(w.size()));auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<h<<"\",\"license\":\"test\",\"externalData\":\"https://example.invalid/x.bin\"}";f.close();auto r=validatePackage(d);CHECK(!r);std::filesystem::remove_all(d);}
TEST("M01 manifest schema and provider are allow-listed"){auto make=[](std::string name,int schema,std::string provider){auto d=std::filesystem::temp_directory_path()/name;std::filesystem::remove_all(d);std::filesystem::create_directories(d);Bytes w{0x3a,0x00};std::ofstream(d/"weights.onnx",std::ios::binary).write(reinterpret_cast<const char*>(w.data()),std::streamsize(w.size()));auto h=sha256(w).hex();std::ofstream f(d/"manifest.json");f<<"{\"schema\":"<<schema<<",\"taskId\":\"sam21.image.v1\",\"weights\":\"weights.onnx\",\"sha256\":\""<<h<<"\",\"license\":\"test\",\"provider\":\""<<provider<<"\"}";f.close();return d;};auto badSchema=make("drawborad-model-schema",2,"cpu");CHECK(!validatePackage(badSchema));std::filesystem::remove_all(badSchema);auto badProvider=make("drawborad-model-provider",1,"remote");CHECK(!validatePackage(badProvider));std::filesystem::remove_all(badProvider);}

TEST("M01 two-stage SAM package verifies decoder digest independently"){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-two-stage";
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    Bytes encoder{0x3a,0x00},decoder{0x3a,0x00};
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

TEST("M01 SAM 2.1 package rejects missing decoder pair"){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-missing-decoder";
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    Bytes encoder{0x3a,0x00};
    std::ofstream(d/"encoder.onnx",std::ios::binary)
        .write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
    std::ofstream manifest(d/"manifest.json");
    manifest<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\","
            "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
            "\"license\":\"test\",\"provider\":\"cpu\"}";
    manifest.close();
    auto result=validatePackage(d);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
    std::filesystem::remove_all(d);
}

namespace {
void pbVarint(Bytes& out,std::uint64_t v){while(v>=0x80){out.push_back(std::uint8_t(v)|0x80);v>>=7;}out.push_back(std::uint8_t(v));}
void pbBytes(Bytes& out,std::uint32_t field,std::span<const std::uint8_t> data){pbVarint(out,(std::uint64_t(field)<<3)|2);pbVarint(out,data.size());out.insert(out.end(),data.begin(),data.end());}
Bytes onnxWithExternalInitializer(std::string_view location){
    Bytes entry,key{'l','o','c','a','t','i','o','n'},value(location.begin(),location.end());
    pbBytes(entry,1,key);pbBytes(entry,2,value);
    Bytes tensor;pbBytes(tensor,13,entry);pbVarint(tensor,(14u<<3)|0u);pbVarint(tensor,1);
    Bytes graph;pbBytes(graph,5,tensor);Bytes model;pbBytes(model,7,graph);return model;
}
Bytes onnxWithNodeDomain(std::string_view domain){
    Bytes node,domainBytes(domain.begin(),domain.end());
    pbBytes(node,7,domainBytes);
    Bytes graph;pbBytes(graph,1,node);
    Bytes model;pbBytes(model,7,graph);
    return model;
}
std::filesystem::path externalDataPackage(std::string_view location){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-external-data";
    std::filesystem::remove_all(d);std::filesystem::create_directories(d);
    auto encoder=onnxWithExternalInitializer(location);Bytes decoder{0x3a,0x00};
    std::ofstream(d/"encoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
    std::ofstream(d/"decoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));
    std::ofstream f(d/"manifest.json");
    f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\","
      "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
      "\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<sha256(decoder).hex()<<"\","
      "\"license\":\"test\",\"provider\":\"cpu\"}";
    return d;
}
}
TEST("M01 ONNX external tensor data is rejected before runtime load"){
    auto d=externalDataPackage("../escape.bin");
    auto result=validatePackage(d);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::PathEscape);
    CHECK(result.error().messageKey=="model.onnx-external-data");
    std::filesystem::remove_all(d);
}

TEST("M01 verified package carries canonical adapter tensor contract"){
    auto d=pkg(false);
    auto manifest=validatePackage(d);
    CHECK(manifest);
    CHECK(manifest.value().abi=="sam21.image.v1");
    CHECK(manifest.value().inputs.size()==5);
    CHECK(manifest.value().inputs[0].name=="image");
    CHECK(manifest.value().inputs[2].dtype=="int64");
    CHECK(manifest.value().outputs.size()==2);
    CHECK(manifest.value().outputs[0].name=="low_res_masks");
    std::filesystem::remove_all(d);
}


TEST("M01 duplicate top-level manifest keys are rejected"){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-duplicate-key";
    std::filesystem::remove_all(d);std::filesystem::create_directories(d);
    Bytes encoder{0x3a,0x00},decoder{0x3a,0x00};
    std::ofstream(d/"encoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
    std::ofstream(d/"decoder.onnx",std::ios::binary).write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));
    std::ofstream f(d/"manifest.json");
    f<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\",\"taskId\":\"unknown.task\","
      "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
      "\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<sha256(decoder).hex()<<"\","
      "\"license\":\"test\",\"provider\":\"cpu\"}";
    f.close();
    auto result=validatePackage(d);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
    std::filesystem::remove_all(d);
}

TEST("M01 ONNX custom operator domains are rejected before runtime load"){
    auto d=std::filesystem::temp_directory_path()/"drawborad-model-custom-op";
    std::filesystem::remove_all(d);std::filesystem::create_directories(d);
    auto encoder=onnxWithNodeDomain("evil.example");
    Bytes decoder{0x3a,0x00};
    std::ofstream(d/"encoder.onnx",std::ios::binary)
        .write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
    std::ofstream(d/"decoder.onnx",std::ios::binary)
        .write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));
    std::ofstream manifest(d/"manifest.json");
    manifest<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\","
            "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
            "\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<sha256(decoder).hex()<<"\","
            "\"license\":\"test\",\"provider\":\"cpu\"}";
    manifest.close();
    auto result=validatePackage(d);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedAdapter);
    CHECK(result.error().messageKey=="model.onnx-custom-domain");
    std::filesystem::remove_all(d);
}


TEST("M01 install owns verified package and blocks active removal"){
    auto source=pkg(false);
    auto root=std::filesystem::temp_directory_path()/"drawborad-owned-model-store";
    std::filesystem::remove_all(root);
    ModelRegistry registry(root);
    auto id=registry.install(source,TrustDecision::UserAcceptedUntrusted);
    CHECK(id);
    auto installed=registry.manifest(id.value());
    CHECK(installed);
    auto ownedEncoder=installed.value().resolvedWeights;
    auto ownedDecoder=installed.value().resolvedDecoderWeights;
    CHECK(std::filesystem::exists(ownedEncoder));
    CHECK(std::filesystem::exists(ownedDecoder));
    auto canonicalRoot=std::filesystem::weakly_canonical(root).string();
    CHECK(std::filesystem::weakly_canonical(ownedEncoder).string().rfind(canonicalRoot,0)==0);
    std::filesystem::remove_all(source);
    CHECK(std::filesystem::exists(ownedEncoder));
    CHECK(registry.retain(id.value()));
    auto blocked=registry.remove(id.value());
    CHECK(!blocked);
    CHECK(blocked.error().code==ErrorCode::Locked);
    registry.release(id.value());
    CHECK(registry.remove(id.value()));
    CHECK(!std::filesystem::exists(ownedEncoder));
    std::filesystem::remove_all(root);
}


TEST("M01 package identity includes decoder digest") {
    auto base=std::filesystem::temp_directory_path()/"drawborad-model-identity";
    auto store=std::filesystem::temp_directory_path()/"drawborad-model-identity-store";
    std::filesystem::remove_all(base);
    std::filesystem::remove_all(store);
    std::filesystem::create_directories(base/"a");
    std::filesystem::create_directories(base/"b");
    Bytes encoder{0x3a,0x00};
    Bytes decoderA{0x3a,0x00,0x08,0x01};
    Bytes decoderB{0x3a,0x00,0x08,0x02};
    auto writePackage=[&](const std::filesystem::path& dir,const Bytes& decoder) {
        std::ofstream(dir/"encoder.onnx",std::ios::binary)
            .write(reinterpret_cast<const char*>(encoder.data()),std::streamsize(encoder.size()));
        std::ofstream(dir/"decoder.onnx",std::ios::binary)
            .write(reinterpret_cast<const char*>(decoder.data()),std::streamsize(decoder.size()));
        std::ofstream manifest(dir/"manifest.json");
        manifest<<"{\"schema\":1,\"taskId\":\"sam21.image.v1\","
                "\"weights\":\"encoder.onnx\",\"sha256\":\""<<sha256(encoder).hex()<<"\","
                "\"decoderWeights\":\"decoder.onnx\",\"decoderSha256\":\""<<sha256(decoder).hex()<<"\","
                "\"license\":\"test\",\"provider\":\"cpu\"}";
    };
    writePackage(base/"a",decoderA);
    writePackage(base/"b",decoderB);
    ModelRegistry registry(store);
    auto a=registry.install(base/"a",TrustDecision::UserAcceptedUntrusted);
    auto b=registry.install(base/"b",TrustDecision::UserAcceptedUntrusted);
    CHECK(a);
    CHECK(b);
    CHECK(a.value()!=b.value());
    CHECK(registry.manifest(a.value()).value().decoderSha256!=
          registry.manifest(b.value()).value().decoderSha256);
    std::filesystem::remove_all(base);
    std::filesystem::remove_all(store);
}
