// Opt-in research helper only. Compiles the unchanged repository core.
// A declaration is an operator assertion of the COMPLETE immutable prefix, not
// semantic primer discovery. Do not use reorder if that prefix is unknown.
// Synthetic disjoint tests may use zero internally; the public API never does.
#include "core/Document.h"
#include "render/CoverageRasterizer.h"
#include "storage/ProjectStore.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>

using namespace drawborad;
namespace fs = std::filesystem;
namespace {
constexpr std::size_t MaxEvents=8192, MaxSamples=500000, MaxInputBytes=32*1024*1024;
constexpr std::size_t MaxDeclarationBytes=256, MaxOutputFiles=128;
constexpr std::uint64_t MaxOutputBytes=128*1024*1024;
constexpr std::uint64_t GapUs=80000;
std::string binaryHash;
void check(bool condition,const std::string& why) { if(!condition) throw std::runtime_error(why); }
template<class T> T take(Result<T> r) { if(!r) throw std::runtime_error(r.error().messageKey); return std::move(r.value()); }
void take(Result<void> r) { if(!r) throw std::runtime_error(r.error().messageKey); }
std::string idhex(Id id) { std::ostringstream o;for(auto b:id.bytes)o<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return o.str(); }
std::string quote(const std::string& s) {
    std::ostringstream o;o<<'"';for(unsigned char c:s) {
        if(c=='"'||c=='\\')o<<'\\'<<c;
        else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else o<<c;
    }o<<'"';return o.str();
}
// Refuse symlinks in existing path components, including dangling links. This
// helper assumes its input/output parent directories are not concurrently renamed.
void rejectSymlinks(const fs::path& p) {
    fs::path current;
    for(const auto& part:fs::absolute(p)) {
        current/=part;
        check(fs::symlink_status(current).type()!=fs::file_type::symlink,"symlink path forbidden: "+current.string());
    }
}
Hash fileHash(const fs::path& p,std::uint64_t maximum) {
    rejectSymlinks(p);
    check(fs::is_regular_file(p)&&fs::file_size(p)<=maximum,"file size/type budget: "+p.string());
    std::ifstream f(p,std::ios::binary);check(bool(f),"cannot read file");Sha256State h;std::array<std::uint8_t,65536> b{};
    std::uint64_t total=0;while(f){f.read(reinterpret_cast<char*>(b.data()),b.size());const auto n=f.gcount();total+=std::uint64_t(n);check(total<=maximum,"growing file budget");h.update(std::span(b).first(std::size_t(n)));}
    check(f.eof()&&!f.bad(),"hash read failed");return h.finish();
}
void mkdirNew(const fs::path& p) {
    rejectSymlinks(p);
    // create_directory is the atomic no-clobber operation; no check-then-create.
    check(fs::create_directory(p),"output exists; no clobber");
}
void writeBytes(const fs::path& p,const Bytes& b) {
    rejectSymlinks(p);
    NativeOutputFile f;take(f.createExclusive(p));take(f.write(b));take(f.close());
}
void writeText(const fs::path& p,const std::string& s) { writeBytes(p,Bytes(s.begin(),s.end())); }
// status.json is the final completion marker. Absence or passed:false means
// partial/failure, even when some native files or diagnostic exports exist.
class OutputDirectory {
    fs::path path;
    bool complete=false;
public:
    explicit OutputDirectory(fs::path p):path(std::move(p)) {mkdirNew(path);}
    ~OutputDirectory() {
        if(!complete)try {writeText(path/"status.json","{\"passed\":false,\"status\":\"partial_or_failed\"}\n");}catch(...) {}
    }
    void finish() {writeText(path/"status.json","{\"passed\":true,\"status\":\"complete\"}\n");complete=true;}
};
void rejectSidecars(const fs::path& p) {
    for(const auto* suffix:{".bak",".journal"}) {
        const auto extra=fs::path(p.string()+suffix);
        check(fs::symlink_status(extra).type()==fs::file_type::not_found,
              "ambiguous input sidecar forbidden: "+extra.string());
    }
}
// ProjectStore's normal limit is larger than this research contract. Enforce
// our bound on the actual read as well as the preflight hash, and never allow
// recovery to read a backup or journal that appeared after the preflight.
class ResearchInputIO final:public NativeFileIO {
    fs::path input;
public:
    explicit ResearchInputIO(fs::path p):input(std::move(p)) {}
    Result<Bytes> read(const fs::path& p,std::uint64_t maximum) const override {
        if(p!=input)return error(ErrorCode::CorruptData,"research.alternate-input-forbidden");
        rejectSidecars(input);rejectSymlinks(p);
        check(fs::is_regular_file(p),"research input must be a regular file");
        return NativeFileIO::read(p,std::min<std::uint64_t>(maximum,MaxInputBytes));
    }
};
Bytes readBounded(const fs::path& p,std::size_t maximum) {
    rejectSymlinks(p);
    check(fs::is_regular_file(p)&&fs::file_size(p)<=maximum,"file size/type budget: "+p.string());
    return take(NativeFileIO{}.read(p,maximum));
}
struct Box { std::int64_t x0,y0,x1,y1; };
bool overlap(Box a,Box b) {return a.x0<a.x1&&a.y0<a.y1&&b.x0<b.x1&&b.y0<b.y1&&a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1;}
struct Block {std::size_t first,end;std::string stage;NodeId object,layer;};
unsigned stageRank(const std::string& stage) {
    if(stage=="base")return 0;
    if(stage=="form")return 1;
    if(stage=="detail")return 2;
    throw std::runtime_error("only base/form/detail stages supported");
}
struct Inspection {std::vector<Box> boxes;std::vector<std::size_t> blockOf;std::vector<Block> blocks;std::size_t samples=0,phaseRuns=0,estimatedOutputFiles=0;std::uint64_t estimatedOutputBytes=0;};
Inspection inspect(const DocumentSnapshot& s) {
    take(validateSnapshot(s));
    check(s.spec.pixelSize.width>=1&&s.spec.pixelSize.height>=1&&s.spec.pixelSize.width<=512&&s.spec.pixelSize.height<=512,"dimensions must be 1..512");
    check(s.spec.colorSpec.format==PixelFormat{ColorMode::RGB,Depth::U8,true},"requires native RGBA8");
    check(!s.eventIndex->empty()&&s.eventIndex->size()<=MaxEvents,"event budget");
    check(s.nodes->size()<=256&&s.resources->size()<=128,"node/resource budget");
    const auto w=std::int64_t(s.spec.pixelSize.width),h=std::int64_t(s.spec.pixelSize.height);
    for(const auto& [id,r]:*s.resources) {
        check(r.kind==ResourceKind::Mask,"only Mask resources allowed; source/reference forbidden");
        check(r.bytes->size()==std::size_t(w*h),"mask size mismatch");
        check(sha256(*r.bytes)==id,"mask digest mismatch");
        check(std::all_of(r.bytes->begin(),r.bytes->end(),[](auto b){return b==0||b==255;}),"only binary review masks allowed");
    }
    std::size_t objects=0,layers=0;
    for(const auto& [id,n]:*s.nodes) {
        (void)id;check(n.sourceKind==SourceKind::Reconstructed&&n.sourceResource.empty()&&n.content.empty(),"source content forbidden");
        check(n.transform==LayerTransform{},"transforms outside conservative-bbox research contract");
        check(n.blend==BlendMode::Normal,"only normal blending allowed");
        if(n.kind==NodeKind::Object)++objects;
        if(n.kind==NodeKind::Layer){++layers;(void)stageRank(n.stage);}
    }
    // Export is bounded: <=48 raw layer images, <=3 phase images and <=9
    // temporal pairs, each at <=512x512. No arbitrary stage/file explosion.
    check(objects<=16&&layers<=48,"export budget: at most 16 objects and 48 layers");
    for(auto layer:layerOrder(s))check(nativeLayerSupported(s,layer),"unsupported layer compositing");
    Inspection in;
    for(std::size_t i=0;i<s.eventIndex->size();++i) {
        const auto& e=s.eventIndex->at(i);take(validateRasterEvent(e));
        check(e.style.width<=128,"128 nib budget");
        check(e.geometry.size()<=MaxSamples-in.samples,"sample budget");in.samples+=e.geometry.size();
        check(s.nodes->contains(e.layerId)&&s.nodes->at(e.layerId).kind==NodeKind::Layer,"event layer missing");
        check(s.nodes->at(e.layerId).objectId==e.objectId&&s.nodes->contains(e.objectId)&&s.nodes->at(e.objectId).kind==NodeKind::Object,"event object mismatch");
        check(e.geometry.front().timestampUs==e.startUs&&e.geometry.back().timestampUs==e.endUs,"event endpoints/time mismatch");
        for(const auto& p:e.geometry)check(p.timestampUs>=e.startUs&&p.timestampUs<=e.endUs,"sample outside event time");
        if(i)check(e.startUs>=s.eventIndex->at(i-1).endUs&&e.startUs-s.eventIndex->at(i-1).endUs==GapUs,"requires uniform 80000us inter-event gap");
        const auto r=take(strokeBounds(e));
        const auto x0=std::clamp(r.x,std::int64_t(0),w),y0=std::clamp(r.y,std::int64_t(0),h);
        const auto x1=std::clamp(r.x+std::int64_t(r.width),std::int64_t(0),w),y1=std::clamp(r.y+std::int64_t(r.height),std::int64_t(0),h);
        in.boxes.push_back({x0,y0,x1,y1});const auto& stage=s.nodes->at(e.layerId).stage;
        if(i)check(stageRank(s.nodes->at(s.eventIndex->at(i-1).layerId).stage)<=stageRank(stage),"stages must be monotone base/form/detail");
        if(!i||s.nodes->at(s.eventIndex->at(i-1).layerId).stage!=stage)++in.phaseRuns;
        if(in.blocks.empty()||in.blocks.back().stage!=stage||in.blocks.back().layer!=e.layerId||in.blocks.back().object!=e.objectId)
            in.blocks.push_back({i,i+1,stage,e.objectId,e.layerId});
        else in.blocks.back().end=i+1;
        in.blockOf.push_back(in.blocks.size()-1);
    }
    // Conservative complete reorder/export estimate, before any output mkdir:
    // 32 MiB native project, 2 KiB/event for metadata+mapping, 1 MiB manifest
    // and headers, 7 bytes/pixel final RGBA+PPM, 4/layer, and 25/phase
    // (one phase RGBA plus three temporal RGBA+PPM pairs). Count includes
    // native file/lock, metadata, mapping, checks, status and exports manifest.
    in.estimatedOutputFiles=9+layers+7*in.phaseRuns;
    in.estimatedOutputBytes=MaxInputBytes+1024*1024+2048*s.eventIndex->size()
        +std::uint64_t(w*h)*(7+4*layers+25*in.phaseRuns);
    check(in.phaseRuns<=3&&in.estimatedOutputFiles<=MaxOutputFiles&&in.estimatedOutputBytes<=MaxOutputBytes,"export file/byte budget");
    return in;
}
DocumentSnapshot recover(const fs::path& p) {
    rejectSidecars(p);const auto before=fileHash(p,MaxInputBytes);
    auto result=take(ProjectStore{std::make_shared<ResearchInputIO>(p)}.recover(p));
    check(result.warnings.empty(),"recovery warning; refuse ambiguous input");
    rejectSidecars(p);check(fileHash(p,MaxInputBytes)==before,"input changed during recovery");
    (void)inspect(result.snapshot);return result.snapshot;
}
std::string metadata(const DocumentSnapshot& s,const Inspection& in) {
    std::ostringstream o;o<<std::setprecision(17)<<"{\"schemaVersion\":1,\"binarySha256\":"<<quote(binaryHash)<<",\"width\":"<<s.spec.pixelSize.width<<",\"height\":"<<s.spec.pixelSize.height
      <<",\"eventCount\":"<<s.eventIndex->size()<<",\"sampleCount\":"<<in.samples<<",\"revision\":"<<s.revision<<",\"gapUs\":"<<GapUs
      <<",\"exportBudget\":{\"estimatedBytes\":"<<in.estimatedOutputBytes<<",\"estimatedFiles\":"<<in.estimatedOutputFiles<<",\"maxBytes\":"<<MaxOutputBytes<<",\"maxFiles\":"<<MaxOutputFiles<<",\"phaseRuns\":"<<in.phaseRuns<<",\"maxObjects\":16,\"maxLayers\":48},\"boundsConvention\":\"clipped native strokeBounds, half-open\",\"sourceAssetReads\":0,\"stateHash\":"<<quote(take(stateHash(s)).hex())<<",\"resources\":[";
    bool comma=false;for(const auto& [id,r]:*s.resources){if(comma)o<<',';comma=true;o<<"{\"id\":"<<quote(id.hex())<<",\"sha256\":"<<quote(sha256(*r.bytes).hex())<<",\"kind\":\"Mask\",\"bytes\":"<<r.bytes->size()<<'}';}
    o<<"],\"blocks\":[";for(std::size_t i=0;i<in.blocks.size();++i){if(i)o<<',';const auto& b=in.blocks[i];o<<"{\"index\":"<<i<<",\"first\":"<<b.first<<",\"end\":"<<b.end<<",\"stage\":"<<quote(b.stage)<<",\"objectId\":"<<quote(idhex(b.object))<<",\"layerId\":"<<quote(idhex(b.layer))<<'}';}
    o<<"],\"events\":[";
    for(std::size_t i=0;i<s.eventIndex->size();++i){if(i)o<<',';const auto& e=s.eventIndex->at(i);const auto b=in.boxes[i];
        o<<"{\"ordinal\":"<<i<<",\"id\":"<<quote(idhex(e.id))<<",\"objectId\":"<<quote(idhex(e.objectId))<<",\"layerId\":"<<quote(idhex(e.layerId))<<",\"stage\":"<<quote(s.nodes->at(e.layerId).stage)<<",\"block\":"<<in.blockOf[i]
         <<",\"startUs\":"<<e.startUs<<",\"endUs\":"<<e.endUs<<",\"durationUs\":"<<e.endUs-e.startUs<<",\"sampleCount\":"<<e.geometry.size()<<",\"first\":["<<e.geometry.front().x<<','<<e.geometry.front().y<<"],\"last\":["<<e.geometry.back().x<<','<<e.geometry.back().y
         <<"],\"bounds\":["<<b.x0<<','<<b.y0<<','<<b.x1<<','<<b.y1<<"]}";}
    o<<"]}\n";return o.str();
}
std::size_t parseSize(std::string_view s) {std::size_t n{};const auto [end,err]=std::from_chars(s.data(),s.data()+s.size(),n);check(err==std::errc{}&&end==s.data()+s.size(),"invalid unsigned integer");return n;}
struct Declaration {
    std::string fileSha256,stateSha256;
    std::size_t eventCount=0,immutablePrefixCount=0;
};
std::string declarationText(const Declaration& d) {
    return "DBLOCALITY1 "+d.fileSha256+" "+d.stateSha256+" "+std::to_string(d.eventCount)+" "+std::to_string(d.immutablePrefixCount)+"\n";
}
bool lowercaseHash(const std::string& s) {
    return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
Declaration parseDeclaration(const std::string& text) {
    check(text.size()<=MaxDeclarationBytes,"declaration size budget");
    std::istringstream in(text);std::string magic,file,state,count,prefix,extra;
    check(bool(in>>magic>>file>>state>>count>>prefix)&&!(in>>extra),"declaration requires exactly five fields");
    check(magic=="DBLOCALITY1"&&lowercaseHash(file)&&lowercaseHash(state),"invalid declaration magic/hash");
    Declaration d{file,state,parseSize(count),parseSize(prefix)};
    check(d.eventCount>=1&&d.eventCount<=MaxEvents&&d.immutablePrefixCount>=1&&d.immutablePrefixCount<=d.eventCount,"declaration prefix/count out of range");
    check(text==declarationText(d),"declaration must be one canonical line without trailing data");return d;
}
Declaration readDeclaration(const fs::path& p) {
    const auto b=readBounded(p,MaxDeclarationBytes);return parseDeclaration(std::string(b.begin(),b.end()));
}
void verifyDeclaration(const Declaration& d,const Hash& fileHashValue,const Hash& stateHashValue,std::size_t count) {
    check(d.fileSha256==fileHashValue.hex(),"declaration input file hash mismatch");
    check(d.stateSha256==stateHashValue.hex(),"declaration native state hash mismatch");
    check(d.eventCount==count,"declaration event count mismatch");
    check(d.immutablePrefixCount>=1&&d.immutablePrefixCount<=count,"declaration prefix out of range");
}
void declarePrefix(const fs::path& input,std::size_t prefix,const fs::path& destination) {
    check(prefix>=1&&prefix<=MaxEvents,"immutable prefix must be nonempty and bounded");
    const auto file=fileHash(input,MaxInputBytes);const auto s=recover(input);const auto state=take(stateHash(s));
    check(prefix<=s.eventIndex->size(),"immutable prefix exceeds event count");
    rejectSidecars(input);check(fileHash(input,MaxInputBytes)==file,"input changed while declaring");
    // Exclusive creation is the last operation; existing files, aliases and
    // dangling symlinks cannot be clobbered. No semantic inference is claimed.
    writeText(destination,declarationText({file.hex(),state.hex(),s.eventIndex->size(),prefix}));
}
std::vector<std::size_t> readPermutation(const fs::path& p,std::size_t n) {
    const auto b=readBounded(p,MaxEvents*12);std::istringstream f(std::string(b.begin(),b.end()));
    std::string token;std::vector<std::size_t> result;
    while(f>>token){check(result.size()<n&&token.size()<=10,"permutation length/token budget");result.push_back(parseSize(token));}
    check(f.eof()&&!f.bad()&&result.size()==n,"permutation count mismatch");return result;
}
void requireResearch(std::span<const std::string_view> args) {
    check(!args.empty()&&args.front()=="--research","explicit --research is required before the operation");
}
struct Verification {std::uint64_t overlapPairs=0,withinBlockPairs=0;};
Verification verifyPermutation(const Inspection& in,const std::vector<std::size_t>& permutation,std::size_t primer) {
    const auto n=in.boxes.size();check(n<=MaxEvents&&permutation.size()==n&&primer<=n,"permutation/primer count");
    std::vector<std::size_t> inverse(n,n);for(std::size_t j=0;j<n;++j){auto i=permutation[j];check(i<n&&inverse[i]==n,"permutation is not bijective");inverse[i]=j;check(in.blockOf[i]==in.blockOf[j],"permutation crosses contiguous phase/layer/object block");if(j<primer)check(i==j,"primer prefix changed");}
    Verification v;
    for(std::size_t i=0;i<n;++i)for(std::size_t j=i+1;j<n;++j)if(overlap(in.boxes[i],in.boxes[j])){++v.overlapPairs;if(in.blockOf[i]==in.blockOf[j])++v.withinBlockPairs;check(inverse[i]<inverse[j],"overlapping native bounding-box order inverted: "+std::to_string(i)+","+std::to_string(j));}
    return v;
}
DocumentSnapshot reorderUnchecked(const DocumentSnapshot& s,const std::vector<std::size_t>& order,std::size_t primer) {
    check(s.revision<UINT64_MAX,"revision overflow");
    auto result=take(Document::fromSnapshot(s)).snapshot();result.revision=s.revision+1;
    auto events=std::make_shared<EventList>();events->reserve(order.size());std::uint64_t cursor=s.eventIndex->front().startUs;
    std::set<Id> used;for(std::size_t j=0;j<primer;++j)used.insert(s.eventIndex->at(j).id);
    for(std::size_t j=0;j<order.size();++j){auto e=s.eventIndex->at(order[j]);const auto start=e.startUs,duration=e.endUs-start;
        if(j>=primer){e.id=Id::fromNumber(j+1);const std::array<std::uint8_t,8> tag{'L','O','C','A','L','I','T','Y'};std::copy(tag.begin(),tag.end(),e.id.bytes.begin());check(used.insert(e.id).second,"generated id collision");}
        e.startUs=cursor;e.endUs=take(checkedAdd(cursor,duration));for(auto& p:e.geometry)p.timestampUs=take(checkedAdd(cursor,p.timestampUs-start));
        events->push_back(std::move(e));if(j+1<order.size())cursor=take(checkedAdd(events->back().endUs,GapUs));
    }
    result.eventIndex=events;take(validateSnapshot(result));return result;
}
void verifyProjection(const DocumentSnapshot& old,const DocumentSnapshot& next,const std::vector<std::size_t>& permutation,std::size_t primer) {
    check(old.documentId==next.documentId&&old.spec==next.spec&&*old.nodes==*next.nodes&&*old.resources==*next.resources,"unaffected snapshot data changed");
    check(old.eventIndex!=next.eventIndex&&next.revision==old.revision+1,"requires independent event index and new revision");
    check(old.eventIndex->size()==next.eventIndex->size(),"event count changed");
    for(std::size_t j=0;j<permutation.size();++j){const auto& before=old.eventIndex->at(permutation[j]);const auto& after=next.eventIndex->at(j);auto restored=after;
        check(after.endUs-after.startUs==before.endUs-before.startUs,"duration changed");check(after.parentRevision==before.parentRevision,"parentRevision changed");restored.id=before.id;restored.startUs=before.startUs;restored.endUs=before.endUs;
        for(auto& p:restored.geometry)p.timestampUs=take(checkedAdd(before.startUs,p.timestampUs-after.startUs));
        check(restored==before,"full StrokeEvent field projection changed");if(j<primer)check(after==before,"primer field changed");
    }
    check(old.eventIndex->back().endUs==next.eventIndex->back().endUs,"total duration changed");(void)inspect(next);
}
TileData blank(const DocumentSnapshot& s) {
    TileData t;t.width=std::uint32_t(s.spec.pixelSize.width);t.height=std::uint32_t(s.spec.pixelSize.height);t.rowStride=std::uint64_t(t.width)*4;t.format=s.spec.colorSpec.format;t.profileId=s.spec.colorSpec.profileId;t.bytes.resize(std::size_t(t.rowStride*t.height));
    if(!s.spec.background.transparent)for(std::size_t i=0;i<t.bytes.size();i+=4){for(unsigned c=0;c<3;++c)t.bytes[i+c]=std::uint8_t(std::lround(s.spec.background.paperColor.channels[c]*255));t.bytes[i+3]=255;}
    return t;
}
void over(TileData& d,const TileData& s,double opacity) {
    for(std::size_t p=0;p<d.bytes.size();p+=4){const auto sa=unsigned(std::lround(s.bytes[p+3]*opacity)),da=unsigned(d.bytes[p+3]),den=sa*255+da*(255-sa);if(!den)continue;for(unsigned c=0;c<3;++c){const auto num=unsigned(s.bytes[p+c])*sa*255+unsigned(d.bytes[p+c])*da*(255-sa);d.bytes[p+c]=std::uint8_t((num+den/2)/den);}d.bytes[p+3]=std::uint8_t((den+127)/255);}
}
TileData renderLayer(ReferenceRenderer& r,const DocumentSnapshot& s,NodeId id,std::uint64_t ticks=UINT64_MAX,std::uint64_t last=UINT64_MAX) {
    auto raw=take(r.renderRegion(s,id,{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height},{s.revision,last,ticks}));return take(nativeMaskedTile(s,id,std::move(raw),0,0));
}
TileData render(ReferenceRenderer& r,const DocumentSnapshot& s,std::uint64_t ticks=UINT64_MAX,std::uint64_t last=UINT64_MAX) {
    auto t=blank(s);for(auto id:layerOrder(s))if(layerVisible(s,id))over(t,renderLayer(r,s,id,ticks,last),s.nodes->at(id).opacity);return t;
}
void noSources(const ReferenceRenderer& r) {check(r.statistics().sourceAssetReads==0,"unexpected source asset read");}
void writePPM(const fs::path& p,const TileData& t) {
    std::ostringstream h;h<<"P6\n"<<t.width<<' '<<t.height<<"\n255\n";const auto text=h.str();Bytes data(text.begin(),text.end());data.reserve(data.size()+std::size_t(t.width)*t.height*3);
    for(std::size_t i=0;i<t.bytes.size();i+=4)for(unsigned c=0;c<3;++c)data.push_back(std::uint8_t((unsigned(t.bytes[i+c])*t.bytes[i+3]+255u*(255u-t.bytes[i+3])+127)/255));
    writeBytes(p,data);
}
struct Phase {std::size_t first,end;std::string name;};
std::vector<Phase> phases(const DocumentSnapshot& s) {
    std::vector<Phase> result;for(std::size_t i=0;i<s.eventIndex->size();++i){const auto& name=s.nodes->at(s.eventIndex->at(i).layerId).stage;if(result.empty()||result.back().name!=name)result.push_back({i,i+1,name});else result.back().end=i+1;}return result;
}
// Every image is rendered from the saved/reopened native snapshot. Frames use
// real time cutoffs, including points strictly inside a selected stroke.
void exportEvidence(const DocumentSnapshot& s,const fs::path& out,const DocumentSnapshot* original=nullptr) {
    ReferenceRenderer baseline;
    ReferenceRenderer warm;const auto final=render(warm,s);writeBytes(out/"final.rgba",final.bytes);writePPM(out/"final.ppm",final);
    if(original)check(render(baseline,*original)==final,"INTEGRITY FAILURE: final differs from original");
    check(render(warm,s)==final,"warm renderer mismatch");ReferenceRenderer fresh;check(render(fresh,s)==final,"fresh renderer mismatch");
    std::ostringstream manifest;manifest<<"{\"width\":"<<s.spec.pixelSize.width<<",\"height\":"<<s.spec.pixelSize.height<<",\"final\":\"final.rgba\",\"layers\":[";
    const auto layers=layerOrder(s);for(std::size_t i=0;i<layers.size();++i){const auto id=layers[i];const auto t=renderLayer(warm,s,id);if(original)check(renderLayer(baseline,*original,id)==t,"INTEGRITY FAILURE: layer differs from original");const auto filename="layer-"+std::to_string(i)+".rgba";writeBytes(out/filename,t.bytes);if(i)manifest<<',';manifest<<"{\"id\":"<<quote(idhex(id))<<",\"stage\":"<<quote(s.nodes->at(id).stage)<<",\"file\":"<<quote(filename)<<",\"sha256\":"<<quote(sha256(t.bytes).hex())<<'}';}
    manifest<<"],\"phases\":[";const auto ps=phases(s);for(std::size_t i=0;i<ps.size();++i){const auto& p=ps[i];const auto t=render(warm,s,s.eventIndex->at(p.end-1).endUs);if(original)check(render(baseline,*original,original->eventIndex->at(p.end-1).endUs)==t,"INTEGRITY FAILURE: phase end differs from original");const auto filename="phase-"+std::to_string(i)+".rgba";writeBytes(out/filename,t.bytes);if(i)manifest<<',';manifest<<"{\"stage\":"<<quote(p.name)<<",\"first\":"<<p.first<<",\"end\":"<<p.end<<",\"file\":"<<quote(filename)<<",\"sha256\":"<<quote(sha256(t.bytes).hex())<<'}';}
    // Visibility toggles use separate immutable node maps, never mutate shared state.
    for(auto id:layers){auto hidden=s;auto nodes=std::make_shared<NodeMap>(*s.nodes);nodes->at(id).visible=false;hidden.nodes=nodes;const auto a=render(warm,hidden);ReferenceRenderer cold;check(render(cold,hidden)==a,"hide cold/warm mismatch");noSources(cold);auto shown=hidden;shown.nodes=std::make_shared<const NodeMap>(*s.nodes);check(render(warm,shown)==final,"hide/show restore mismatch");}
    manifest<<"],\"timeSamples\":[";bool comma=false;for(std::size_t i=0;i<ps.size();++i){const auto& p=ps[i];const auto ordinal=p.first+(p.end-p.first)/2;const auto& e=s.eventIndex->at(ordinal);const std::array<std::uint64_t,3> ticks{e.startUs,e.startUs+(e.endUs-e.startUs)/2,e.endUs};for(unsigned j=0;j<3;++j){const auto t=render(warm,s,ticks[j]);const auto filename="time-"+std::to_string(i)+"-"+std::to_string(j);writeBytes(out/(filename+".rgba"),t.bytes);writePPM(out/(filename+".ppm"),t);if(comma)manifest<<',';comma=true;manifest<<"{\"stage\":"<<quote(p.name)<<",\"eventOrdinal\":"<<ordinal<<",\"sample\":"<<quote(j==0?"start":j==1?"mid":"end")<<",\"ticks\":"<<ticks[j]<<",\"eventStart\":"<<e.startUs<<",\"eventEnd\":"<<e.endUs<<",\"strictlyWithinStroke\":"<<(ticks[j]>e.startUs&&ticks[j]<e.endUs?"true":"false")<<",\"file\":"<<quote(filename+".rgba")<<",\"sha256\":"<<quote(sha256(t.bytes).hex())<<'}';}}
    noSources(warm);noSources(fresh);noSources(baseline);manifest<<"],\"sourceAssetReads\":0,\"warmFreshEqual\":true,\"hideShowRestoresEqual\":true}\n";writeText(out/"exports.json",manifest.str());
}
void reorderProject(const fs::path& input,const fs::path& permutationPath,const fs::path& declarationPath,const fs::path& out) {
    const auto declaration=readDeclaration(declarationPath);const auto declarationHash=fileHash(declarationPath,MaxDeclarationBytes);
    const auto acceptedDeclaration=declarationText(declaration);
    check(declarationHash==sha256(Bytes(acceptedDeclaration.begin(),acceptedDeclaration.end())),"declaration changed while reading");
    const auto sourceHash=fileHash(input,MaxInputBytes);const auto old=recover(input);const auto oldHash=take(stateHash(old));
    verifyDeclaration(declaration,sourceHash,oldHash,old.eventIndex->size());const auto primer=declaration.immutablePrefixCount;
    const auto in=inspect(old);const auto perm=readPermutation(permutationPath,old.eventIndex->size());const auto v=verifyPermutation(in,perm,primer);
    const auto next=reorderUnchecked(old,perm,primer);verifyProjection(old,next,perm,primer);check(take(stateHash(old))==oldHash,"source snapshot mutated");
    // All permutation and projection checks above are complete BEFORE mkdir.
    rejectSidecars(input);check(fileHash(input,MaxInputBytes)==sourceHash,"input changed before output");
    OutputDirectory output(out);take(ProjectStore{}.save(out/"reordered.draw",next));const auto reopened=recover(out/"reordered.draw");check(reopened.sameContent(next)&&reopened.revision==next.revision,"save/reopen mismatch");verifyProjection(old,reopened,perm,primer);
    writeText(out/"metadata.json",metadata(reopened,inspect(reopened)));
    std::vector<std::size_t> inverse(perm.size());for(std::size_t j=0;j<perm.size();++j)inverse[perm[j]]=j;
    std::ostringstream map;map<<"[";for(std::size_t i=0;i<perm.size();++i){if(i)map<<',';const auto j=inverse[i];const auto& before=old.eventIndex->at(i);const auto& e=reopened.eventIndex->at(j);map<<"{\"oldOrdinal\":"<<i<<",\"newOrdinal\":"<<j<<",\"oldId\":"<<quote(idhex(before.id))<<",\"newId\":"<<quote(idhex(e.id))<<",\"oldStartUs\":"<<before.startUs<<",\"startUs\":"<<e.startUs<<",\"endUs\":"<<e.endUs<<'}';}map<<"]\n";writeText(out/"mapping.json",map.str());
    exportEvidence(reopened,out,&old);rejectSidecars(input);check(fileHash(input,MaxInputBytes)==sourceHash&&take(stateHash(old))==oldHash,"source changed during export");
    std::ostringstream checks;checks<<"{\"schemaVersion\":1,\"passed\":true,\"declarationSha256\":"<<quote(declarationHash.hex())<<",\"immutablePrefixCount\":"<<primer<<",\"binarySha256\":"<<quote(binaryHash)<<",\"inputFileSha256\":"<<quote(sourceHash.hex())<<",\"inputStateHash\":"<<quote(oldHash.hex())<<",\"outputStateHash\":"<<quote(take(stateHash(reopened)).hex())<<",\"eventCount\":"<<perm.size()<<",\"sampleCount\":"<<in.samples<<",\"primerCount\":"<<primer<<",\"overlapPairsVerified\":"<<v.overlapPairs<<",\"withinBlockOverlapPairs\":"<<v.withinBlockPairs<<",\"permutationBijection\":true,\"primerVerbatim\":true,\"blockBoundariesFixed\":true,\"allOverlapOrderPreserved\":true,\"fullStrokeEventProjectionEqual\":true,\"relativePointTimesEqual\":true,\"durationEqual\":true,\"gapUs\":80000,\"unaffectedSnapshotContentEqual\":true,\"independentEventIndex\":true,\"savedReopenedEqual\":true,\"originalUnchanged\":true,\"originalFinalLayerPhasePixelsEqual\":true,\"sourceAssetReads\":0}\n";writeText(out/"checks.json",checks.str());output.finish();
    std::cout<<"reordered events="<<perm.size()<<" samples="<<in.samples<<" overlapPairs="<<v.overlapPairs<<" sourceAssetReads=0\n";
}

DocumentSnapshot synthetic(bool transparent=true) {
    DocumentSnapshot s;s.documentId=Id::fromNumber(900);s.revision=3;s.spec.pixelSize={64,48};s.spec.tileCount=1;s.spec.estimatedBytes=64*48*4;s.spec.background.transparent=transparent;s.spec.background.paperColor.channels={.9,.85,.8,0};
    auto ns=std::make_shared<NodeMap>();LayerNode root;root.id=Document::rootId();root.kind=NodeKind::Root;root.name="root";root.children={Id::fromNumber(10)};ns->emplace(root.id,root);
    LayerNode object;object.id=Id::fromNumber(10);object.parent=root.id;object.kind=NodeKind::Object;object.name="object";object.children={Id::fromNumber(100)};object.bounds={0,0,64,48};
    Bytes mask(64*48,255);Resource resource{sha256(mask),ResourceKind::Mask,std::make_shared<const Bytes>(mask),{}};object.maskId=resource.id;
    auto rs=std::make_shared<ResourceMap>();rs->emplace(resource.id,resource);s.resources=rs;ns->emplace(object.id,object);
    LayerNode layer;layer.id=Id::fromNumber(100);layer.parent=object.id;layer.objectId=object.id;layer.name="base";layer.stage="base";layer.bounds={0,0,64,48};ns->emplace(layer.id,layer);s.nodes=ns;return s;
}
StrokeEvent stroke(unsigned i,double x,double y,double x2,double y2,bool chisel=false) {
    StrokeEvent e;e.id=Id::fromNumber(10000+i);e.objectId=Id::fromNumber(10);e.layerId=Id::fromNumber(100);e.parentRevision=2;e.engineVersion=2;e.seed=987654321;e.toolId="marker";e.color.channels=i%2?std::array<double,4>{.1,.3,.9,0}:std::array<double,4>{.9,.2,.05,0};e.color.alpha=.7;
    e.style.width=5.5;e.style.hardness=.24;e.style.opacity=.4;e.style.shape=chisel?NibShape::Chisel:NibShape::Round;e.style.angle=3;e.startUs=1000000+i*180000;e.endUs=e.startUs+100000;
    e.geometry={{x,y,.3,12.,-4.,e.startUs,2,5},{(x+x2)/2,(y+y2)/2,.9,13.,-5.,e.startUs+37000,2,5},{x2,y2,.5,14.,-6.,e.endUs,2,5}};return e;
}
void setEvents(DocumentSnapshot& s,EventList events) {std::uint64_t t=1000000;for(auto& e:events){const auto old=e.startUs,d=e.endUs-old;e.startUs=t;e.endUs=t+d;for(auto& p:e.geometry)p.timestampUs=t+(p.timestampUs-old);t=e.endUs+GapUs;}s.eventIndex=std::make_shared<const EventList>(std::move(events));}
bool rejected(const DocumentSnapshot& s,const std::vector<std::size_t>& order,std::size_t primer=0) {try{(void)verifyPermutation(inspect(s),order,primer);return false;}catch(const std::exception&){return true;}}
void test(bool ok,const std::string& name,std::vector<std::string>& passed){check(ok,"self-test failed: "+name);passed.push_back(name);}
void selfTest(const fs::path& out) {
    OutputDirectory output(out);std::vector<std::string> passed;std::uint64_t sourceReads=0;
    for(bool transparent:{true,false})for(bool chisel:{false,true}){
        auto s=synthetic(transparent);auto a=stroke(0,7.2,9.6,20.1,12.8,chisel),b=stroke(1,40.4,31.5,55.7,34.2,chisel);if(chisel){a.style.grain.resize(64);b.style.grain.resize(64);for(unsigned i=0;i<64;++i){a.style.grain[i]=std::uint8_t(80+(i*29)%176);b.style.grain[i]=std::uint8_t(50+(i*17)%206);}}
        setEvents(s,{a,b});const auto oldHash=take(stateHash(s));const std::vector<std::size_t> perm{1,0};(void)verifyPermutation(inspect(s),perm,0);const auto next=reorderUnchecked(s,perm,0);verifyProjection(s,next,perm,0);
        ReferenceRenderer warm;const auto baseline=render(warm,s);const auto changed=render(warm,next);ReferenceRenderer fresh;test(changed==baseline&&render(fresh,next)==baseline,"disjoint commute "+std::string(transparent?"transparent":"opaque")+(chisel?" chisel fixed grain":" soft round AA"),passed);
        test(take(stateHash(s))==oldHash&&s.eventIndex!=next.eventIndex,"immutable snapshot and fresh index "+std::to_string(passed.size()),passed);
        const auto path=out/("case-"+std::to_string(passed.size())+".draw");take(ProjectStore{}.save(path,next));const auto opened=recover(path);test(opened.sameContent(next)&&opened.revision==next.revision&&render(fresh,opened)==baseline,"save reopen "+std::to_string(passed.size()),passed);
        const auto& original=s.eventIndex->at(1);const auto& translated=next.eventIndex->at(0);for(auto offset:{0ull,25000ull,50000ull,100000ull})test(take(strokeCoverage(original,{0,0,64,48},original.startUs+offset))==take(strokeCoverage(translated,{0,0,64,48},translated.startUs+offset)),"relative time coverage "+std::to_string(passed.size()),passed);
        noSources(warm);noSources(fresh);sourceReads+=warm.statistics().sourceAssetReads+fresh.statistics().sourceAssetReads;
    }
    auto overlapDoc=synthetic();auto a=stroke(0,10,12,36,12),b=stroke(1,22,11,45,14);a.style.width=b.style.width=9;setEvents(overlapDoc,{a,b});
    test(rejected(overlapDoc,{1,0}),"partial overlap negative swap rejected",passed);const auto illegal=reorderUnchecked(overlapDoc,{1,0},0);ReferenceRenderer r;test(render(r,overlapDoc)!=render(r,illegal),"different-color low-alpha negative swap visibly differs",passed);
    auto aa=synthetic();a=stroke(0,8,10,30,10);b=stroke(1,8,14,30,14);a.style.width=b.style.width=8;setEvents(aa,{a,b});test(rejected(aa,{1,0}),"disjoint centerlines but soft AA footprints conflict",passed);
    auto chain=synthetic();a=stroke(0,5,10,17,10);b=stroke(1,19,10,31,10);auto c=stroke(2,33,10,45,10);setEvents(chain,{a,b,c});const auto ci=inspect(chain);test(overlap(ci.boxes[0],ci.boxes[1])&&overlap(ci.boxes[1],ci.boxes[2])&&!overlap(ci.boxes[0],ci.boxes[2])&&rejected(chain,{2,1,0}),"A-B-C conflict chain preserved",passed);
    auto disjoint=synthetic();setEvents(disjoint,{stroke(0,5,5,15,5),stroke(1,40,30,50,30)});test(rejected(disjoint,{1,0},1),"primer prefix protected",passed);test(rejected(disjoint,{0,0})&&rejected(disjoint,{0,2}),"duplicate/out-of-range permutation rejected",passed);
    auto phase=disjoint;auto nodes=std::make_shared<NodeMap>(*phase.nodes);auto newLayer=nodes->at(Id::fromNumber(100));newLayer.id=Id::fromNumber(101);newLayer.stage="detail";nodes->emplace(newLayer.id,newLayer);nodes->at(Id::fromNumber(10)).children.push_back(newLayer.id);phase.nodes=nodes;auto events=*phase.eventIndex;events[1].layerId=newLayer.id;phase.eventIndex=std::make_shared<const EventList>(events);test(rejected(phase,{1,0}),"phase/layer boundary protected",passed);
    auto equalBox=synthetic();a=stroke(0,10,10,30,30);b=stroke(1,10,30,30,10);a.geometry[1].pressure=b.geometry[1].pressure=0;setEvents(equalBox,{a,b});test(rejected(equalBox,{1,0}),"same bounding box stays conservative",passed);
    auto edge=synthetic();edge.spec.pixelSize={512,48};edge.spec.estimatedBytes=512*48*4;auto edgeNodes=std::make_shared<NodeMap>(*edge.nodes);edgeNodes->at(Id::fromNumber(10)).maskId={};edge.nodes=edgeNodes;edge.resources=std::make_shared<const ResourceMap>();setEvents(edge,{stroke(0,508,12,511,13),stroke(1,511,14,514,14)});test(rejected(edge,{1,0}),"AA contact at 512 tile/canvas boundary protected",passed);
    auto zero=disjoint;events=*zero.eventIndex;events[1].style.opacity=0;events[0].geometry[1].x=events[0].geometry[0].x;events[0].geometry[1].y=events[0].geometry[0].y;zero.eventIndex=std::make_shared<const EventList>(events);const auto z=reorderUnchecked(zero,{1,0},0);verifyProjection(zero,z,{1,0},0);test(render(r,zero)==render(r,z),"repeated coordinates and zero effective alpha preserved",passed);
    events=*disjoint.eventIndex;events[1].startUs+=1;events[1].endUs+=1;for(auto& p:events[1].geometry)++p.timestampUs;auto gap=disjoint;gap.eventIndex=std::make_shared<const EventList>(events);test(rejected(gap,{0,1}),"nonuniform gap rejected",passed);
    const auto in=inspect(disjoint);(void)verifyPermutation(in,{1,0},0);const auto reordered=reorderUnchecked(disjoint,{1,0},0);test(reordered.sameContent(reorderUnchecked(disjoint,{1,0},0)),"deterministic regenerated events",passed);
    auto scheduler=synthetic();auto primerStroke=stroke(0,32,24,32,24);primerStroke.style.width=64;primerStroke.style.opacity=1;primerStroke.style.hardness=1;primerStroke.color.alpha=1;primerStroke.color.channels={.8,.8,.8,0};
    auto p1=stroke(1,5,5,9,5),p2=stroke(2,46,36,51,36),p3=stroke(3,19,5,23,5),p4=stroke(4,32,36,36,36);setEvents(scheduler,{primerStroke,p1,p2,p3,p4});
    take(ProjectStore{}.save(out/"scheduler-input.draw",scheduler));writeText(out/"scheduler-known-valid.txt","0\n1\n3\n4\n2\n");
    (void)verifyPermutation(inspect(scheduler),{0,1,3,4,2},1);test(true,"five-stroke fixed-primer scheduler fixture valid",passed);
    take(ProjectStore{}.save(out/"input.draw",disjoint));writeText(out/"valid-permutation.txt","1\n0\n");writeText(out/"invalid-permutation.txt","0\n0\n");
    declarePrefix(out/"input.draw",1,out/"input.declaration");declarePrefix(out/"scheduler-input.draw",1,out/"scheduler.declaration");
    bool rejectedOutput=false;try{reorderProject(out/"input.draw",out/"invalid-permutation.txt",out/"input.declaration",out/"must-not-exist");}catch(const std::exception&){rejectedOutput=true;}test(rejectedOutput&&!fs::exists(out/"must-not-exist"),"invalid reorder creates no output",passed);
    reorderProject(out/"scheduler-input.draw",out/"scheduler-known-valid.txt",out/"scheduler.declaration",out/"valid-output");
    bool clobberRejected=false;try{reorderProject(out/"scheduler-input.draw",out/"scheduler-known-valid.txt",out/"scheduler.declaration",out/"valid-output");}catch(const std::exception&){clobberRejected=true;}test(clobberRejected,"atomic no-clobber existing output",passed);
    noSources(r);test(sourceReads==0&&r.statistics().sourceAssetReads==0,"sourceAssetReads zero",passed);
    // The original 44 synthetic checks above remain. Additional public-contract
    // checks never use a zero-prefix public reorder or open an external asset.
    const auto good=readDeclaration(out/"scheduler.declaration");
    test(parseDeclaration(declarationText(good)).immutablePrefixCount==1,"canonical declaration round trip",passed);
    auto expectInvalidDeclaration=[&](const std::string& name,const std::string& text) {
        const auto file=out/("bad-declaration-"+std::to_string(passed.size())+".txt");
        const auto destination=out/("rejected-output-"+std::to_string(passed.size()));writeText(file,text);
        bool failed=false;try{reorderProject(out/"scheduler-input.draw",out/"scheduler-known-valid.txt",file,destination);}catch(const std::exception&){failed=true;}
        test(failed&&fs::symlink_status(destination).type()==fs::file_type::not_found,name+" rejects before output",passed);
    };
    auto bad=good;bad.fileSha256=std::string(64,'0');expectInvalidDeclaration("wrong input hash",declarationText(bad));
    bad=good;bad.stateSha256=std::string(64,'0');expectInvalidDeclaration("wrong native state hash",declarationText(bad));
    bad=good;++bad.eventCount;expectInvalidDeclaration("wrong event count",declarationText(bad));
    bad=good;bad.immutablePrefixCount=0;expectInvalidDeclaration("zero public prefix",declarationText(bad));
    bad=good;bad.immutablePrefixCount=bad.eventCount+1;expectInvalidDeclaration("prefix exceeds count",declarationText(bad));
    auto wrongMagic=declarationText(good);wrongMagic[0]='X';expectInvalidDeclaration("wrong declaration magic",wrongMagic);
    expectInvalidDeclaration("trailing declaration token",declarationText(good)+"extra\n");
    expectInvalidDeclaration("trailing declaration whitespace",declarationText(good)+"\n");
    expectInvalidDeclaration("oversized declaration",std::string(MaxDeclarationBytes+1,'x'));
    bool failed=false;try{declarePrefix(out/"scheduler-input.draw",0,out/"zero.declaration");}catch(const std::exception&){failed=true;}
    test(failed&&!fs::exists(out/"zero.declaration"),"zero prefix declaration creates no file",passed);
    declarePrefix(out/"scheduler-input.draw",5,out/"full.declaration");writeText(out/"identity.txt","0\n1\n2\n3\n4\n");
    reorderProject(out/"scheduler-input.draw",out/"identity.txt",out/"full.declaration",out/"full-prefix-output");
    test(readDeclaration(out/"full.declaration").immutablePrefixCount==5,"whole input may be explicitly immutable",passed);
    failed=false;try{declarePrefix(out/"scheduler-input.draw",1,out/"scheduler.declaration");}catch(const std::exception&){failed=true;}
    test(failed&&declarationText(readDeclaration(out/"scheduler.declaration"))==declarationText(good),"declaration cannot clobber existing file",passed);
    failed=false;try{const std::array<std::string_view,1> args{"inspect"};requireResearch(args);}catch(const std::exception&){failed=true;}
    test(failed,"missing explicit research opt-in rejected",passed);
    failed=false;try{requireResearch({});}catch(const std::exception&){failed=true;}
    test(failed,"empty public invocation rejected",passed);
    auto altered=reorderUnchecked(disjoint,{1,0},0);auto alteredEvents=std::make_shared<EventList>(*altered.eventIndex);++alteredEvents->front().parentRevision;altered.eventIndex=alteredEvents;
    failed=false;try{verifyProjection(disjoint,altered,{1,0},0);}catch(const std::exception&){failed=true;}
    test(failed,"parentRevision is not normalized away",passed);
    for(const auto* suffix:{".bak",".journal"}) {
        const auto side=fs::path((out/"scheduler-input.draw").string()+suffix);writeText(side,"");
        failed=false;try{(void)recover(out/"scheduler-input.draw");}catch(const std::exception&){failed=true;}
        test(failed,std::string("existing ambiguous sidecar rejected ")+suffix,passed);check(fs::remove(side),"test sidecar cleanup failed");
        fs::create_symlink("missing-sidecar-target",side);
        failed=false;try{(void)recover(out/"scheduler-input.draw");}catch(const std::exception&){failed=true;}
        test(failed,std::string("dangling ambiguous sidecar rejected ")+suffix,passed);check(fs::remove(side),"test sidecar cleanup failed");
    }
    fs::create_symlink("scheduler-input.draw",out/"input-link.draw");
    failed=false;try{(void)recover(out/"input-link.draw");}catch(const std::exception&){failed=true;}
    test(failed,"symlink input rejected",passed);
    fs::create_symlink("missing-output-target",out/"dangling-output");
    failed=false;try{reorderProject(out/"scheduler-input.draw",out/"scheduler-known-valid.txt",out/"scheduler.declaration",out/"dangling-output");}catch(const std::exception&){failed=true;}
    test(failed&&fs::is_symlink(fs::symlink_status(out/"dangling-output")),"dangling output symlink is not clobbered",passed);
    auto wrongStage=disjoint;auto wrongNodes=std::make_shared<NodeMap>(*wrongStage.nodes);wrongNodes->at(Id::fromNumber(100)).stage="arbitrary";wrongStage.nodes=wrongNodes;
    test(rejected(wrongStage,{0,1}),"arbitrary phase labels rejected before export",passed);
    auto backwards=phase;wrongNodes=std::make_shared<NodeMap>(*backwards.nodes);wrongNodes->at(Id::fromNumber(100)).stage="detail";wrongNodes->at(Id::fromNumber(101)).stage="base";backwards.nodes=wrongNodes;
    test(rejected(backwards,{0,1}),"nonmonotone phase runs rejected before export",passed);
    auto softMask=disjoint;auto softNodes=std::make_shared<NodeMap>(*softMask.nodes);auto softBytes=Bytes(64*48,127);Resource softResource{sha256(softBytes),ResourceKind::Mask,std::make_shared<const Bytes>(softBytes),{}};auto softResources=std::make_shared<ResourceMap>();softResources->emplace(softResource.id,softResource);softNodes->at(Id::fromNumber(10)).maskId=softResource.id;softMask.nodes=softNodes;softMask.resources=softResources;
    test(rejected(softMask,{0,1}),"soft review masks rejected",passed);

    std::ostringstream report;report<<"{\"passed\":true,\"binarySha256\":"<<quote(binaryHash)<<",\"sourceAssetReads\":0,\"tests\":[";for(std::size_t i=0;i<passed.size();++i){if(i)report<<',';report<<quote(passed[i]);}report<<"]}\n";writeText(out/"self-tests.json",report.str());output.finish();std::cout<<"self-test passed "<<passed.size()<<" checks; no photo opened or exported\n";
}
} // namespace
int main(int argc,char** argv) {
    try {
        std::vector<std::string_view> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
        requireResearch(args);
        binaryHash=fileHash(fs::canonical(argv[0]),128*1024*1024).hex();
        if(argc==5&&args[1]=="inspect"){
            const auto sourceHash=fileHash(argv[3],MaxInputBytes);const auto s=recover(argv[3]);const auto in=inspect(s);const auto text=metadata(s,in);
            OutputDirectory output(argv[4]);writeText(fs::path(argv[4])/"metadata.json",text);
            rejectSidecars(argv[3]);check(fileHash(argv[3],MaxInputBytes)==sourceHash,"input changed during inspection");output.finish();
            std::cout<<"inspected events="<<s.eventIndex->size()<<" samples="<<in.samples<<" blocks="<<in.blocks.size()<<"\n";
        }
        else if(argc==6&&args[1]=="declare"){
            declarePrefix(argv[3],parseSize(argv[4]),argv[5]);
            std::cout<<"Declared operator-asserted complete immutable prefix; no semantic primer discovery performed\n";
        }
        else if(argc==7&&args[1]=="reorder")reorderProject(argv[3],argv[4],argv[5],argv[6]);
        else if(argc==5&&args[1]=="export"){
            const auto sourceHash=fileHash(argv[3],MaxInputBytes);const auto s=recover(argv[3]);
            OutputDirectory output(argv[4]);writeText(fs::path(argv[4])/"metadata.json",metadata(s,inspect(s)));exportEvidence(s,argv[4]);
            rejectSidecars(argv[3]);check(fileHash(argv[3],MaxInputBytes)==sourceHash,"input changed during export");output.finish();
        }
        else if(argc==4&&args[1]=="self-test")selfTest(argv[3]);
        else throw std::runtime_error("usage: native-locality --research { inspect INPUT NEW_DIR | declare INPUT PREFIX_COUNT NEW_DECLARATION_FILE | reorder INPUT PERMUTATION_FILE DECLARATION_FILE NEW_DIR | export INPUT NEW_DIR | self-test NEW_DIR }");
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
