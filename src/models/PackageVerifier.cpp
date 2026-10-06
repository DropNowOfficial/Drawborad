#include "ModelManifest.h"
#include "AdapterRegistry.h"
#include "storage/FileIO.h"
#include <fstream>
#include <cctype>
#include <map>
#include <string_view>
namespace drawborad { namespace {
enum class JsonAtomKind { String, Unsigned, Other };
struct JsonAtom { JsonAtomKind kind{JsonAtomKind::Other}; std::string text; std::uint64_t number{}; };
using JsonObject = std::map<std::string, JsonAtom>;

void appendUtf8(std::string& out,std::uint32_t cp){
    if(cp<=0x7f)out.push_back(char(cp));
    else if(cp<=0x7ff){out.push_back(char(0xc0|(cp>>6)));out.push_back(char(0x80|(cp&0x3f)));}
    else if(cp<=0xffff){out.push_back(char(0xe0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&0x3f)));out.push_back(char(0x80|(cp&0x3f)));}
    else{out.push_back(char(0xf0|(cp>>18)));out.push_back(char(0x80|((cp>>12)&0x3f)));out.push_back(char(0x80|((cp>>6)&0x3f)));out.push_back(char(0x80|(cp&0x3f)));}
}
class JsonCursor {
    std::string_view s_; std::size_t p_{};
    void ws(){while(p_<s_.size()&&std::isspace(static_cast<unsigned char>(s_[p_])))++p_;}
    static int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
    Result<std::uint32_t> unicode(){
        if(p_+4>s_.size())return error(ErrorCode::CorruptData,"model.manifest-unicode");
        std::uint32_t cp=0;for(int i=0;i<4;i++){auto h=hex(s_[p_++]);if(h<0)return error(ErrorCode::CorruptData,"model.manifest-unicode");cp=(cp<<4)|std::uint32_t(h);}return cp;
    }
    Result<std::string> str(){
        ws();if(p_>=s_.size()||s_[p_]!='"')return error(ErrorCode::CorruptData,"model.manifest-string");++p_;std::string out;
        while(p_<s_.size()){
            unsigned char c=static_cast<unsigned char>(s_[p_++]);
            if(c=='"')return out;
            if(c<0x20)return error(ErrorCode::CorruptData,"model.manifest-control");
            if(c!='\\'){out.push_back(char(c));continue;}
            if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-escape");
            char e=s_[p_++];
            switch(e){
            case '"':out.push_back('"');break;case '\\':out.push_back('\\');break;case '/':out.push_back('/');break;
            case 'b':out.push_back('\b');break;case 'f':out.push_back('\f');break;case 'n':out.push_back('\n');break;case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;
            case 'u':{
                auto first=unicode();if(!first)return first.error();auto cp=first.value();
                if(cp>=0xd800&&cp<=0xdbff){if(p_+2>s_.size()||s_[p_]!='\\'||s_[p_+1]!='u')return error(ErrorCode::CorruptData,"model.manifest-surrogate");p_+=2;auto low=unicode();if(!low)return low.error();if(low.value()<0xdc00||low.value()>0xdfff)return error(ErrorCode::CorruptData,"model.manifest-surrogate");cp=0x10000+((cp-0xd800)<<10)+(low.value()-0xdc00);}
                else if(cp>=0xdc00&&cp<=0xdfff)return error(ErrorCode::CorruptData,"model.manifest-surrogate");
                appendUtf8(out,cp);break;}
            default:return error(ErrorCode::CorruptData,"model.manifest-escape");}
        }
        return error(ErrorCode::CorruptData,"model.manifest-string-end");
    }
    Result<void> skip(unsigned depth){
        if(depth>64)return error(ErrorCode::BudgetExceeded,"model.manifest-depth");
        ws();
        if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-value");
        if(s_[p_]=='"'){auto v=str();if(!v)return v.error();return {};}
        if(s_[p_]=='{'){++p_;ws();if(p_<s_.size()&&s_[p_]=='}'){++p_;return{};}for(;;){auto k=str();if(!k)return k.error();ws();if(p_>=s_.size()||s_[p_++]!=':')return error(ErrorCode::CorruptData,"model.manifest-colon");auto v=skip(depth+1);if(!v)return v.error();ws();if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-object-end");char c=s_[p_++];if(c=='}')return{};if(c!=',')return error(ErrorCode::CorruptData,"model.manifest-object-separator");}}
        if(s_[p_]=='['){++p_;ws();if(p_<s_.size()&&s_[p_]==']'){++p_;return{};}for(;;){auto v=skip(depth+1);if(!v)return v.error();ws();if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-array-end");char c=s_[p_++];if(c==']')return{};if(c!=',')return error(ErrorCode::CorruptData,"model.manifest-array-separator");}}
        const auto begin=p_;while(p_<s_.size()&&s_[p_]!=','&&s_[p_]!=']'&&s_[p_]!='}'&&!std::isspace(static_cast<unsigned char>(s_[p_])))++p_;auto token=s_.substr(begin,p_-begin);
        if(token=="true"||token=="false"||token=="null")return{};
        if(token.empty())return error(ErrorCode::CorruptData,"model.manifest-token");
        std::size_t q=0;if(token[q]=='-')++q;if(q>=token.size()||token[q]<'0'||token[q]>'9')return error(ErrorCode::CorruptData,"model.manifest-token");
        for(;q<token.size();++q){
            if((token[q]<'0'||token[q]>'9')&&token[q]!='.'&&token[q]!='e'&&token[q]!='E'&&token[q]!='+'&&token[q]!='-')
                return error(ErrorCode::CorruptData,"model.manifest-number");
        }
        return {};
    }
    Result<JsonAtom> atom(){
        ws();if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-value");
        if(s_[p_]=='"'){auto v=str();if(!v)return v.error();return JsonAtom{JsonAtomKind::String,std::move(v.value()),0};}
        if(s_[p_]>='0'&&s_[p_]<='9'){
            std::uint64_t value=0;auto begin=p_;while(p_<s_.size()&&s_[p_]>='0'&&s_[p_]<='9'){auto digit=std::uint64_t(s_[p_++]-'0');if(value>(UINT64_MAX-digit)/10)return error(ErrorCode::CorruptData,"model.manifest-number-overflow");value=value*10+digit;}
            if(p_==begin)return error(ErrorCode::CorruptData,"model.manifest-number");
            if(p_<s_.size()&&(s_[p_]=='.'||s_[p_]=='e'||s_[p_]=='E')){p_=begin;auto ignored=skip(1);if(!ignored)return ignored.error();return JsonAtom{};}
            return JsonAtom{JsonAtomKind::Unsigned,{},value};
        }
        auto ignored=skip(1);if(!ignored)return ignored.error();return JsonAtom{};
    }
public:
    explicit JsonCursor(std::string_view s):s_(s){}
    Result<JsonObject> root(){
        ws();if(p_>=s_.size()||s_[p_++]!='{')return error(ErrorCode::CorruptData,"model.manifest-root");JsonObject out;ws();if(p_<s_.size()&&s_[p_]=='}'){++p_;ws();if(p_!=s_.size())return error(ErrorCode::CorruptData,"model.manifest-trailing");return out;}
        for(;;){auto k=str();if(!k)return k.error();if(out.contains(k.value()))return error(ErrorCode::CorruptData,"model.manifest-duplicate-key");ws();if(p_>=s_.size()||s_[p_++]!=':')return error(ErrorCode::CorruptData,"model.manifest-colon");auto v=atom();if(!v)return v.error();out.emplace(std::move(k.value()),std::move(v.value()));ws();if(p_>=s_.size())return error(ErrorCode::CorruptData,"model.manifest-root-end");char c=s_[p_++];if(c=='}')break;if(c!=',')return error(ErrorCode::CorruptData,"model.manifest-root-separator");}
        ws();if(p_!=s_.size())return error(ErrorCode::CorruptData,"model.manifest-trailing");return out;
    }
};
std::optional<std::string> field(const JsonObject& o,const std::string& k){auto it=o.find(k);if(it==o.end()||it->second.kind!=JsonAtomKind::String)return std::nullopt;return it->second.text;}
std::optional<std::uint32_t> uintField(const JsonObject& o,const std::string& k){auto it=o.find(k);if(it==o.end()||it->second.kind!=JsonAtomKind::Unsigned||it->second.number>UINT32_MAX)return std::nullopt;return std::uint32_t(it->second.number);}
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
        if (kind == ProtoKind::Node && field == 7) {
            std::string_view domain(reinterpret_cast<const char*>(payload.data()), payload.size());
            if (!domain.empty() && domain != "ai.onnx" && domain != "ai.onnx.ml")
                return error(ErrorCode::UnsupportedAdapter, "model.onnx-custom-domain");
        }
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
 auto parsed=JsonCursor(j).root();if(!parsed)return parsed.error();const auto& fields=parsed.value();
 if(j.find("http://")!=std::string::npos||j.find("https://")!=std::string::npos||j.find("file://")!=std::string::npos||j.find("ftp://")!=std::string::npos)return error(ErrorCode::PathEscape,"model.external-url");
 auto schema=uintField(fields,"schema");auto task=field(fields,"taskId"),weights=field(fields,"weights"),hash=field(fields,"sha256"),license=field(fields,"license");auto decoderWeights=field(fields,"decoderWeights"),decoderHash=field(fields,"decoderSha256");if(!schema||!task||!weights||!hash||!license)return error(ErrorCode::CorruptData,"model.manifest-fields");if(bool(decoderWeights)!=bool(decoderHash))return error(ErrorCode::CorruptData,"model.decoder-manifest-fields");if(*task=="sam21.image.v1"&&(!decoderWeights||!decoderHash))return error(ErrorCode::CorruptData,"model.sam-decoder-required");if(*schema!=1)return error(ErrorCode::UnsupportedFeature,"model.schema");if(*task!="sam21.image.v1")return error(ErrorCode::UnsupportedAdapter,"model.task");auto provider=field(fields,"provider").value_or("cpu");if(provider!="cpu"&&provider!="cuda")return error(ErrorCode::UnsupportedAdapter,"model.provider");
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
 m.license=*license;m.provider=provider;m.abi=field(fields,"abi").value_or("sam21.image.v1");m.signedPackage=false;AdapterRegistry registry;auto adapter=registry.resolve(m.taskId);if(!adapter)return adapter.error();if(m.abi!=adapter.value().abi)return error(ErrorCode::UnsupportedAdapter,"model.abi");m.inputs=adapter.value().inputs;m.outputs=adapter.value().outputs;return m;
}
}
