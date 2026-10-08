#include "StyleRegistry.h"
#include "core/Digest.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <variant>
namespace drawborad {
namespace {
struct JsonValue {
    std::variant<std::string,double,bool> value;
};
class FlatJson {
    std::string_view input_;
    std::size_t pos_{};
    void ws(){while(pos_<input_.size() && (input_[pos_]==' '||input_[pos_]=='\n'||
        input_[pos_]=='\t'||input_[pos_]=='\r'))++pos_;}
    bool one(char ch){ws();if(pos_>=input_.size()||input_[pos_]!=ch)return false;++pos_;return true;}
    Result<std::string> string(){
        if(!one('"'))return error(ErrorCode::CorruptData,"style.json-string");
        std::string result;
        while(pos_<input_.size()){
            const auto c=input_[pos_++];
            if(c=='"')return result;
            if(c=='\\'||static_cast<unsigned char>(c)<32||result.size()>=128)
                return error(ErrorCode::CorruptData,"style.json-string");
            result.push_back(c);
        }
        return error(ErrorCode::CorruptData,"style.json-string");
    }
    Result<JsonValue> value(){
        ws();
        if(pos_>=input_.size())return error(ErrorCode::CorruptData,"style.json-value");
        if(input_[pos_]=='"'){
            auto result=string();if(!result)return result.error();
            return JsonValue{std::move(result.value())};
        }
        if(input_.substr(pos_,4)=="true"){
            pos_+=4;return JsonValue{true};
        }
        if(input_.substr(pos_,5)=="false"){
            pos_+=5;return JsonValue{false};
        }
        const auto begin=pos_;
        while(pos_<input_.size() && ((input_[pos_]>='0'&&input_[pos_]<='9') ||
              input_[pos_]=='.'||input_[pos_]=='-'||input_[pos_]=='e'||
              input_[pos_]=='E'||input_[pos_]=='+'))++pos_;
        if(pos_==begin)return error(ErrorCode::CorruptData,"style.json-number");
        double number{};
        const auto first=input_.data()+begin,last=input_.data()+pos_;
        auto parsed=std::from_chars(first,last,number);
        if(parsed.ec!=std::errc{}||parsed.ptr!=last||!std::isfinite(number))
            return error(ErrorCode::CorruptData,"style.json-number");
        return JsonValue{number};
    }
public:
    explicit FlatJson(std::string_view input):input_(input){}
    Result<std::map<std::string,JsonValue>> object(){
        if(!one('{'))return error(ErrorCode::CorruptData,"style.json-object");
        std::map<std::string,JsonValue> result;
        if(!one('}')){
            for(;;){
                auto key=string();if(!key)return key.error();
                if(!one(':'))return error(ErrorCode::CorruptData,"style.json-colon");
                auto v=value();if(!v)return v.error();
                if(!result.emplace(std::move(key.value()),std::move(v.value())).second)
                    return error(ErrorCode::CorruptData,"style.json-duplicate-key");
                if(one('}'))break;
                if(!one(','))return error(ErrorCode::CorruptData,"style.json-comma");
            }
        }
        ws();if(pos_!=input_.size())return error(ErrorCode::CorruptData,"style.json-trailing");
        return result;
    }
};
using Fields=std::map<std::string,JsonValue>;
Result<std::string> text(const Fields& obj,std::string_view key){
    auto it=obj.find(std::string(key));
    if(it==obj.end()||!std::holds_alternative<std::string>(it->second.value))
        return error(ErrorCode::CorruptData,"style.missing-text-field");
    return std::get<std::string>(it->second.value);
}
Result<double> number(const Fields& obj,std::string_view key){
    auto it=obj.find(std::string(key));
    if(it==obj.end()||!std::holds_alternative<double>(it->second.value))
        return error(ErrorCode::CorruptData,"style.missing-number-field");
    return std::get<double>(it->second.value);
}
Result<bool> flag(const Fields& obj,std::string_view key){
    auto it=obj.find(std::string(key));
    if(it==obj.end()||!std::holds_alternative<bool>(it->second.value))
        return error(ErrorCode::CorruptData,"style.missing-flag");
    return std::get<bool>(it->second.value);
}
Hash digest(std::string_view s){return sha256(std::span<const std::uint8_t>(
    reinterpret_cast<const std::uint8_t*>(s.data()),s.size()));}
std::uint64_t littleSeed(const Hash& h){
    std::uint64_t result=0;
    for(unsigned i=0;i<8;++i)result=(result<<8)|h.bytes[i];
    return result;
}
Id asId(const Hash& h){Id id;std::copy_n(h.bytes.begin(),16,id.bytes.begin());return id;}
std::uint64_t next(std::uint64_t& state){
    state^=state>>12;state^=state<<25;state^=state>>27;
    return state*2685821657736338717ull;
}
double fraction(std::uint64_t& state){return double(next(state)>>40)/double(1ull<<24);}
}
Result<ReconstructionStyle> StyleRegistry::load(std::string_view id) const {
    static constexpr std::string_view allowed[]={"fidelity","dry-media","marker","pencil","concept-sketch"};
    if(std::find(std::begin(allowed),std::end(allowed),id)==std::end(allowed))
        return error(ErrorCode::UnsupportedFeature,"style.unknown-id");
    const auto file=root_/(std::string(id)+".json");
    std::ifstream in(file,std::ios::binary);
    if(!in)return error(ErrorCode::UnsupportedFeature,"style.offline-preset-missing");
    std::string raw((std::istreambuf_iterator<char>(in)),{});
    if(raw.empty()||raw.size()>8192)
        return error(ErrorCode::BudgetExceeded,"style.manifest-budget");
    FlatJson parser(raw);auto object=parser.object();
    if(!object)return object.error();
    if(object.value().size()!=9)
        return error(ErrorCode::CorruptData,"style.unexpected-fields");
    auto schema=number(object.value(),"schemaVersion");if(!schema)return schema.error();
    auto actual=text(object.value(),"id");if(!actual)return actual.error();
    auto tool=text(object.value(),"tool");if(!tool)return tool.error();
    auto width=number(object.value(),"widthScale");if(!width)return width.error();
    auto opacity=number(object.value(),"opacityScale");if(!opacity)return opacity.error();
    auto hardness=number(object.value(),"hardness");if(!hardness)return hardness.error();
    auto grain=number(object.value(),"grainStrength");if(!grain)return grain.error();
    auto pressure=number(object.value(),"pressureVariation");if(!pressure)return pressure.error();
    auto fidelity=flag(object.value(),"fidelity");if(!fidelity)return fidelity.error();
    if(schema.value()!=1 || actual.value()!=id ||
       (tool.value()!="preserve" && tool.value()!="marker" &&
        tool.value()!="pencil" && tool.value()!="colored-pencil") ||
       width.value()<=0 || width.value()>1 || opacity.value()<=0 || opacity.value()>1 ||
       hardness.value()<0 || hardness.value()>1 || grain.value()<0 || grain.value()>1 ||
       pressure.value()<0 || pressure.value()>1 || fidelity.value()!=(id=="fidelity") ||
       (fidelity.value() && (tool.value()!="preserve" || width.value()!=1 ||
         opacity.value()!=1 || grain.value()!=0 || pressure.value()!=0)))
        return error(ErrorCode::InvalidInput,"style.invalid-preset");
    ReconstructionStyle style;
    style.id=actual.value();style.schemaVersion=1;style.tool=tool.value();
    style.widthScale=width.value();style.opacityScale=opacity.value();
    style.hardness=hardness.value();style.grainStrength=grain.value();
    style.pressureVariation=pressure.value();style.fidelity=fidelity.value();
    style.canonicalJson=raw;
    return style;
}
Result<StyleVariant> makeStyleVariant(const DocumentSnapshot& original,
                                     const ReconstructionStyle& style,std::uint64_t seed){
    auto valid=validateSnapshot(original);if(!valid)return valid.error();
    if(style.schemaVersion!=1 || style.id.empty() || style.canonicalJson.empty() ||
       style.widthScale<=0 || style.widthScale>1 || style.opacityScale<=0 ||
       style.opacityScale>1 || style.grainStrength<0 || style.grainStrength>1 ||
       style.pressureVariation<0 || style.pressureVariation>1 ||
       (style.fidelity && style.id!="fidelity"))
        return error(ErrorCode::InvalidInput,"style.invalid-variant");
    if(!style.fidelity && style.tool!="marker" && style.tool!="pencil" &&
       style.tool!="colored-pencil")
        return error(ErrorCode::UnsupportedFeature,"style.unsupported-brush");
    if(original.eventIndex->size()>200000)
        return error(ErrorCode::BudgetExceeded,"style.variant-events");
    const auto presetDigest=digest(style.canonicalJson);
    const std::string provenance="DrawboradStyle1\nstyle="+style.id+"\nversion="+
        std::to_string(style.schemaVersion)+"\nseed="+std::to_string(seed)+
        "\npreset="+presetDigest.hex()+"\n";
    const auto provenanceHash=digest(provenance);
    const auto documentHash=digest(original.documentId.bytes==Id{}.bytes?
        provenance:provenance+std::string(reinterpret_cast<const char*>(original.documentId.bytes.data()),16));
    auto snapshot=original;
    snapshot.documentId=asId(documentHash);
    if(snapshot.documentId==original.documentId)
        return error(ErrorCode::InvalidInput,"style.document-collision");
    auto resources=std::make_shared<ResourceMap>(*original.resources);
    Bytes resourceBytes(provenance.begin(),provenance.end());
    Resource resource;
    resource.id=provenanceHash;
    resource.kind=ResourceKind::Reference;
    resource.bytes=std::make_shared<const Bytes>(std::move(resourceBytes));
    auto collision=resources->find(resource.id);
    if(collision!=resources->end()){
        if(collision->second.kind!=ResourceKind::Reference ||
           *collision->second.bytes!=*resource.bytes)
            return error(ErrorCode::DigestMismatch,"style.provenance-collision");
    }else{
        resources->emplace(resource.id,std::move(resource));
    }
    snapshot.resources=resources;
    auto events=std::make_shared<EventList>(*original.eventIndex);
    for(std::size_t i=0;i<events->size();++i){
        auto& event=(*events)[i];
        const std::string signature=provenance+std::to_string(i)+
            std::string(reinterpret_cast<const char*>(event.id.bytes.data()),16);
        const auto eventDigest=digest(signature);
        event.id=asId(eventDigest);
        if(style.fidelity)continue;
        event.seed=littleSeed(eventDigest);
        event.toolId=style.tool;
        event.engineVersion=2;
        event.style.width*=style.widthScale;
        event.style.opacity*=style.opacityScale;
        event.style.hardness=style.hardness;
        std::uint64_t rng=littleSeed(eventDigest)|1ull;
        if(style.grainStrength>0){
            event.style.grain.resize(64);
            for(auto& byte:event.style.grain){
                const double intensity=255.0*(1.0-style.grainStrength*fraction(rng));
                byte=std::uint8_t(std::clamp(intensity,0.0,255.0));
            }
        }
        if(style.pressureVariation>0){
            for(auto& point:event.geometry){
                point.pressure=std::clamp(point.pressure*
                    (1.0-style.pressureVariation*fraction(rng)),0.0,1.0);
            }
        }
    }
    snapshot.eventIndex=events;
    auto checked=validateSnapshot(snapshot);
    if(!checked)return checked.error();
    return StyleVariant{std::move(snapshot),style,seed,provenanceHash};
}
} // namespace drawborad
