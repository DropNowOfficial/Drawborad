#include "ModelManifest.h"
#include "storage/FileIO.h"
#include <fstream>
#include <cctype>
#include <regex>
namespace drawborad { namespace {
std::optional<std::string> field(const std::string&s,const std::string&k){std::regex r("\""+k+"\"\\s*:\\s*\"([^\"]*)\"");std::smatch m;if(std::regex_search(s,m,r))return m[1].str();return std::nullopt;}
std::optional<std::uint32_t> uintField(const std::string&s,const std::string&k){std::regex r("\""+k+"\"\\s*:\\s*([0-9]+)");std::smatch m;if(!std::regex_search(s,m,r))return std::nullopt;try{auto v=std::stoull(m[1].str());if(v>UINT32_MAX)return std::nullopt;return std::uint32_t(v);}catch(...){return std::nullopt;}}
bool dangerousExt(const std::filesystem::path&p){auto e=p.extension().string();for(auto&c:e)c=char(std::tolower(static_cast<unsigned char>(c)));return e==".py"||e==".pyc"||e==".pkl"||e==".pickle"||e==".dll"||e==".exe"||e==".bat"||e==".cmd"||e==".ps1";}
enum class ProtoKind { Model, Graph, Node, Attribute, Tensor, SparseTensor, TrainingInfo, Function };

Result<std::uint64_t> protoVarint(std::span<const std::uint8_t> bytes, std::size_t& at) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        if (at >= bytes.size())
            return error(ErrorCode::CorruptData, "model.onnx-varint");
        const auto byte = bytes[at++];
        value |= std::uint64_t(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0)
            return value;
    }
    return error(ErrorCode::CorruptData, "model.onnx-varint-overflow");
}

std::optional<ProtoKind> childKind(ProtoKind kind, std::uint32_t field) {
    switch (kind) {
    case ProtoKind::Model:
        if (field == 7) return ProtoKind::Graph;
        if (field == 20) return ProtoKind::TrainingInfo;
        if (field == 25) return ProtoKind::Function;
        break;
    case ProtoKind::Graph:
        if (field == 1) return ProtoKind::Node;
        if (field == 5) return ProtoKind::Tensor;
        if (field == 15) return ProtoKind::SparseTensor;
        break;
    case ProtoKind::Node:
        if (field == 5) return ProtoKind::Attribute;
        break;
    case ProtoKind::Attribute:
        if (field == 5 || field == 10) return ProtoKind::Tensor;
        if (field == 6 || field == 11) return ProtoKind::Graph;
        if (field == 22 || field == 23) return ProtoKind::SparseTensor;
        break;
    case ProtoKind::SparseTensor:
        if (field == 1 || field == 2) return ProtoKind::Tensor;
        break;
    case ProtoKind::TrainingInfo:
        if (field == 1 || field == 2) return ProtoKind::Graph;
        break;
    case ProtoKind::Function:
        if (field == 7) return ProtoKind::Node;
        if (field == 11) return ProtoKind::Attribute;
        break;
    case ProtoKind::Tensor:
        break;
    }
    return std::nullopt;
}

Result<bool> scanExternalData(std::span<const std::uint8_t> bytes, ProtoKind kind,
                              unsigned depth = 0) {
    if (depth > 64)
        return error(ErrorCode::BudgetExceeded, "model.onnx-recursion");
    std::size_t at = 0;
    while (at < bytes.size()) {
        auto keyResult = protoVarint(bytes, at);
        if (!keyResult) return keyResult.error();
        const auto key = keyResult.value();
        const auto field = std::uint32_t(key >> 3);
        const auto wire = std::uint8_t(key & 7u);
        if (field == 0)
            return error(ErrorCode::CorruptData, "model.onnx-field-zero");

        if (wire == 0) {
            auto value = protoVarint(bytes, at);
            if (!value) return value.error();
            if (kind == ProtoKind::Tensor && field == 14 && value.value() == 1)
                return true;
            continue;
        }
        if (wire == 1) {
            if (bytes.size() - at < 8)
                return error(ErrorCode::CorruptData, "model.onnx-fixed64");
            at += 8;
            continue;
        }
        if (wire == 5) {
            if (bytes.size() - at < 4)
                return error(ErrorCode::CorruptData, "model.onnx-fixed32");
            at += 4;
            continue;
        }
        if (wire != 2)
            return error(ErrorCode::CorruptData, "model.onnx-wire-type");

        auto lengthResult = protoVarint(bytes, at);
        if (!lengthResult) return lengthResult.error();
        const auto length = lengthResult.value();
        if (length > bytes.size() - at || length > SIZE_MAX)
            return error(ErrorCode::CorruptData, "model.onnx-length");
        auto payload = bytes.subspan(at, std::size_t(length));
        at += std::size_t(length);

        if (kind == ProtoKind::Tensor && field == 13)
            return true;
        if (auto child = childKind(kind, field)) {
            auto found = scanExternalData(payload, *child, depth + 1);
            if (!found) return found.error();
            if (found.value()) return true;
        }
    }
    return false;
}

Result<void> validateOnnxStorage(std::span<const std::uint8_t> bytes) {
    auto external = scanExternalData(bytes, ProtoKind::Model);
    if (!external)
        return external.error();
    if (external.value())
        return error(ErrorCode::PathEscape, "model.onnx-external-data");
    return {};
}

Result<Hash> parseHash(std::string_view s){if(s.size()!=64)return error(ErrorCode::MissingDigest,"model.sha256");Hash h;auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};for(std::size_t i=0;i<32;i++){auto a=nib(s[i*2]),b=nib(s[i*2+1]);if(a<0||b<0)return error(ErrorCode::MissingDigest,"model.sha256");h.bytes[i]=std::uint8_t((a<<4)|b);}return h;}
}
Result<ModelManifest> validatePackage(const PackagePath& root){
 std::error_code ec;if(!std::filesystem::is_directory(root,ec)||ec)return error(ErrorCode::InvalidInput,"model.package-dir");
 for(auto it=std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied,ec);it!=std::filesystem::recursive_directory_iterator();it.increment(ec)){if(ec)return error(ErrorCode::IoError,"model.walk");if(it->is_symlink(ec))return error(ErrorCode::PathEscape,"model.symlink");if(it->is_regular_file(ec)&&dangerousExt(it->path()))return error(ErrorCode::UnsupportedFeature,"model.executable-content");}
 auto mp=root/"manifest.json";std::ifstream f(mp,std::ios::binary);if(!f)return error(ErrorCode::CorruptData,"model.manifest");std::string j((std::istreambuf_iterator<char>(f)),{});if(j.size()>1024*1024)return error(ErrorCode::BudgetExceeded,"model.manifest-size");
 if(j.find("http://")!=std::string::npos||j.find("https://")!=std::string::npos||j.find("file://")!=std::string::npos||j.find("ftp://")!=std::string::npos)return error(ErrorCode::PathEscape,"model.external-url");
 auto schema=uintField(j,"schema");auto task=field(j,"taskId"),weights=field(j,"weights"),hash=field(j,"sha256"),license=field(j,"license");auto decoderWeights=field(j,"decoderWeights"),decoderHash=field(j,"decoderSha256");if(!schema||!task||!weights||!hash||!license)return error(ErrorCode::CorruptData,"model.manifest-fields");if(bool(decoderWeights)!=bool(decoderHash))return error(ErrorCode::CorruptData,"model.decoder-manifest-fields");if(*task=="sam21.image.v1"&&(!decoderWeights||!decoderHash))return error(ErrorCode::CorruptData,"model.sam-decoder-required");if(*schema!=1)return error(ErrorCode::UnsupportedFeature,"model.schema");if(*task!="sam21.image.v1")return error(ErrorCode::UnsupportedAdapter,"model.task");auto provider=field(j,"provider").value_or("cpu");if(provider!="cpu"&&provider!="cuda")return error(ErrorCode::UnsupportedAdapter,"model.provider");
 std::filesystem::path rel=*weights;if(rel.is_absolute()||rel.empty()||rel.string().find("..")!=std::string::npos||dangerousExt(rel))return error(ErrorCode::PathEscape,"model.weights-path");auto full=std::filesystem::weakly_canonical(root/rel,ec);auto canon=std::filesystem::weakly_canonical(root,ec);if(ec||full.string().rfind(canon.string(),0)!=0)return error(ErrorCode::PathEscape,"model.weights-path");NativeFileIO io;auto bytes=io.read(full,4*GiB);if(!bytes)return bytes.error();auto expected=parseHash(*hash);if(!expected)return expected.error();auto actual=sha256(bytes.value());if(actual!=expected.value())return error(ErrorCode::DigestMismatch,"model.weights-digest");auto onnxStorage=validateOnnxStorage(bytes.value());if(!onnxStorage)return onnxStorage.error();
 ModelManifest m;m.taskId=*task;m.weights=rel;m.resolvedWeights=full;m.sha256=actual;
 if(decoderWeights){
  std::filesystem::path drel=*decoderWeights;
  if(drel.is_absolute()||drel.empty()||drel.string().find("..")!=std::string::npos||dangerousExt(drel))return error(ErrorCode::PathEscape,"model.decoder-path");
  auto dfull=std::filesystem::weakly_canonical(root/drel,ec);
  if(ec||dfull.string().rfind(canon.string(),0)!=0)return error(ErrorCode::PathEscape,"model.decoder-path");
  auto dbytes=io.read(dfull,4*GiB);if(!dbytes)return dbytes.error();
  auto dexp=parseHash(*decoderHash);if(!dexp)return dexp.error();
  auto dactual=sha256(dbytes.value());if(dactual!=dexp.value())return error(ErrorCode::DigestMismatch,"model.decoder-digest");auto decoderStorage=validateOnnxStorage(dbytes.value());if(!decoderStorage)return decoderStorage.error();
  m.decoderWeights=drel;m.resolvedDecoderWeights=dfull;m.decoderSha256=dactual;
 }
 m.license=*license;m.provider=provider;m.abi=field(j,"abi").value_or("sam21.image.v1");m.signedPackage=field(j,"signature").has_value();return m;
}
}
