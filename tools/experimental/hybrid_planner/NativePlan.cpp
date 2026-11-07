// Experimental brush-plan ingress and evidence runner. No Python rasterizer.
#include "core/Commands.h"
#include "reconstruct/ReconstructionService.h"
#include "render/ReferenceRenderer.h"
#include "storage/ProjectStore.h"
#include "view/LayerTree.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace drawborad;
namespace {
Bytes readExact(const std::filesystem::path& p,std::size_t expected) {
    if(!std::filesystem::is_regular_file(p) || std::filesystem::file_size(p)!=expected)
        throw std::runtime_error("input must be a regular file of the expected byte size");
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("input read failed");
    Bytes bytes(expected);f.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(expected));
    if(std::size_t(f.gcount())!=expected)throw std::runtime_error("input truncated during read");
    char extra{};if(f.get(extra))throw std::runtime_error("input grew during read");
    if(f.bad())throw std::runtime_error("input read failed");
    return bytes;
}
void write(const std::filesystem::path& p,const Bytes& bytes) {
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!f)throw std::runtime_error("output write failed");
}
template<class T>T unwrap(Result<T> result) {
    if(!result)throw std::runtime_error(result.error().messageKey);
    return std::move(result.value());
}
std::string hexId(Id id) {std::ostringstream out;for(auto b:id.bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return out.str();}
void require(Result<void> result) {if(!result)throw std::runtime_error(result.error().messageKey);}
void over(TileData& d,const TileData& s) {
    for(std::size_t p=0;p<d.bytes.size();p+=4) {
        const auto sa=unsigned(s.bytes[p+3]),da=unsigned(d.bytes[p+3]);
        const auto den=sa*255+da*(255-sa);if(!den)continue;
        for(unsigned c=0;c<3;++c) {
            const auto num=unsigned(s.bytes[p+c])*sa*255+unsigned(d.bytes[p+c])*da*(255-sa);
            d.bytes[p+c]=std::uint8_t((num+den/2)/den);
        }
        d.bytes[p+3]=std::uint8_t((den+127)/255);
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc<5)throw std::runtime_error("usage: native-plan plan.txt source.rgba output-dir mask ...");
        const std::filesystem::path output(argv[3]);
        if(std::filesystem::exists(output))throw std::runtime_error("output exists; no clobber");
        if(!std::filesystem::is_regular_file(argv[1]) || std::filesystem::file_size(argv[1])>16*1024*1024)
            throw std::runtime_error("plan file budget");
        std::ifstream input(argv[1]);std::string magic;unsigned w{},h{},objects{},count{},baselineCount{};
        input>>magic>>w>>h>>objects>>count>>baselineCount;
        if(!input || magic!="DBHYBRID1" || !w || !h || w>512 || h>512 ||
           !objects || objects>16 || argc!=int(objects)+4 || !count || count>2048 || !baselineCount || baselineCount>count || count-baselineCount>192)
            throw std::runtime_error("invalid prototype plan header");
        DocumentSnapshot initial;initial.documentId=Id::fromNumber(0x535441474544);initial.revision=1;
        initial.spec.pixelSize={w,h};initial.spec.estimatedBytes=std::uint64_t(w)*h*4;initial.spec.tileCount=1;
        initial.spec.background.transparent=true;
        auto nodes=std::make_shared<NodeMap>();auto resources=std::make_shared<ResourceMap>();
        LayerNode root;root.id=Document::rootId();root.kind=NodeKind::Root;root.name="Automatic hybrid research";
        nodes->emplace(root.id,root);initial.nodes=nodes;initial.resources=resources;
        ReviewedObjects reviewed;reviewed.confirmedRevision=1;reviewed.canReconstruct=true;
        Bytes claimed(std::size_t(w)*h,0);
        for(unsigned o=0;o<objects;++o) {
            auto mask=readExact(argv[4+o],std::size_t(w)*h);
            bool present=false;
            for(std::size_t p=0;p<mask.size();++p) {
                if(mask[p]!=0 && mask[p]!=255)throw std::runtime_error("nonbinary mask");
                if(mask[p] && claimed[p])throw std::runtime_error("overlapping masks");
                if(mask[p]) {present=true;claimed[p]=255;}
            }
            if(!present)throw std::runtime_error("empty mask");
            Resource resource{sha256(mask),ResourceKind::Mask,std::make_shared<const Bytes>(mask),{}};
            resources->emplace(resource.id,resource);reviewed.masks.push_back(std::move(mask));
            LayerNode node;node.id=Id::fromNumber(10+o);node.parent=root.id;node.kind=NodeKind::Object;
            node.name="Reviewed object "+std::to_string(o+1);node.bounds={0,0,w,h};node.maskId=resource.id;
            nodes->emplace(node.id,node);nodes->at(root.id).children.push_back(node.id);
        }
        TileData source;source.width=w;source.height=h;source.rowStride=std::uint64_t(w)*4;
        source.format=initial.spec.colorSpec.format;source.profileId=initial.spec.colorSpec.profileId;
        source.bytes=readExact(argv[2],std::size_t(w)*h*4);
        for(std::size_t p=0;p<claimed.size();++p)
            if(claimed[p] && source.bytes[p*4+3]!=255)throw std::runtime_error("opaque reviewed source required");
        const std::array<std::string,3> phases{"base","form","detail"};
        const std::array<std::string,3> names{"Base color","Form and local color","Structure and detail"};
        EventList events;std::map<NodeId,EventBatch> batches;std::size_t samples=0;int lastPhase=-1;
        std::uint64_t time=1000000;
        for(unsigned ordinal=0;ordinal<count;++ordinal) {
            unsigned object{},phase{},n{};double width{},hardness{},opacity{},r{},g{},b{},alpha{};
            input>>object>>phase>>width>>hardness>>opacity>>r>>g>>b>>alpha>>n;
            if(!input || object>=objects || phase>2 || int(phase)<lastPhase ||
               !std::isfinite(width) || width<=0 || width>128 || !n || n>250000 || samples+n>250000)
                throw std::runtime_error("invalid stroke header or phase/sample budget");
            if(ordinal>=baselineCount && (phase!=2 || n<2))throw std::runtime_error("corrections must be continuous detail paths");
            lastPhase=int(phase);samples+=n;double pathLength=0;
            const auto objectId=Id::fromNumber(10+object),layerId=Id::fromNumber(100+object*3+phase);
            if(!nodes->contains(layerId)) {
                LayerNode layer;layer.id=layerId;layer.parent=objectId;layer.objectId=objectId;layer.bounds={0,0,w,h};
                layer.stage=phases[phase];layer.name=names[phase];nodes->emplace(layerId,layer);
                nodes->at(objectId).children.push_back(layerId);
            }
            StrokeEvent e;e.id=Id::fromNumber(10000+ordinal);e.objectId=objectId;e.layerId=layerId;
            e.parentRevision=1;e.engineVersion=2;e.seed=20261107;e.toolId=phase==2?"pencil":"marker";
            e.style.width=width;e.style.opacity=opacity;e.style.hardness=hardness;e.color.channels={r,g,b,0};e.color.alpha=alpha;
            e.color.profileId=source.profileId;e.color.activeChannels=3;e.startUs=time;
            for(unsigned point=0;point<n;++point) {
                double x{},y{},pressure{};input>>x>>y>>pressure;
                if(!input || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) ||
                   x<0 || y<0 || x>=w || y>=h || pressure<0 || pressure>1 ||
                   !reviewed.masks[object][std::size_t(y)*w+std::size_t(x)])throw std::runtime_error("sample outside reviewed mask");
                if(!e.geometry.empty()) {
                    const auto& before=e.geometry.back();const auto distance=std::hypot(x-before.x,y-before.y);
                    if(ordinal>=baselineCount && distance==0)throw std::runtime_error("duplicate correction point");
                    pathLength+=distance;
                    const auto checks=std::max(1u,unsigned(std::ceil(distance*4)));
                    for(unsigned k=1;k<=checks;++k) {
                        const auto t=double(k)/checks,px=before.x+(x-before.x)*t,py=before.y+(y-before.y)*t;
                        if(!reviewed.masks[object][std::size_t(py)*w+std::size_t(px)])throw std::runtime_error("segment outside reviewed mask");
                    }
                    time+=std::uint64_t(std::max(1000.0,distance*(phase==2?5000:2500)));
                }
                e.geometry.push_back({x,y,pressure,{},{},time,0,0});
            }
            if(ordinal>=baselineCount && pathLength<3)throw std::runtime_error("correction path shorter than 3 pixels");
            e.endUs=time;time+=80000;
            auto [it,inserted]=batches.try_emplace(layerId,EventBatch{1,objectId,layerId,{}});(void)inserted;
            it->second.events.push_back(e);events.push_back(std::move(e));
        }
        std::string trailing;if(input>>trailing)throw std::runtime_error("trailing plan data");
        require(validateSnapshot(initial));
        for(const auto& [id,batch]:batches) {
            const auto object=std::size_t(std::find(nodes->at(root.id).children.begin(),nodes->at(root.id).children.end(),batch.objectId)-nodes->at(root.id).children.begin());
            ReconstructionRequest request;request.snapshot=initial;request.reviewed=reviewed;request.source={&source};
            request.maskIndex=object;request.objectId=batch.objectId;request.layerId=id;request.proposedEvents=batch.events;
            request.budget={2048,250000,128};unwrap(ReconstructionService{}.compile(request));
        }
        auto document=unwrap(Document::fromSnapshot(initial));Command command;
        for(const auto& event:events)command.operations.emplace_back(Paint{event,unwrap(strokeBounds(event))});
        unwrap(document.apply(command,initial.revision));auto snapshot=document.snapshot();
        if(!std::filesystem::create_directories(output))throw std::runtime_error("output exists; no clobber");
        require(ProjectStore{}.save(output/"automatic.draw",snapshot));
        auto recovered=unwrap(ProjectStore{}.recover(output/"automatic.draw"));
        if(!recovered.snapshot.sameContent(snapshot))throw std::runtime_error("save/reopen mismatch");
        snapshot=recovered.snapshot;source.bytes.clear();
        ReferenceRenderer renderer;const auto order=layerOrder(snapshot);
        const auto render=[&](std::uint64_t limit,std::uint64_t ticks) {
            TileData dest;dest.width=w;dest.height=h;dest.rowStride=std::uint64_t(w)*4;dest.format=snapshot.spec.colorSpec.format;
            dest.profileId=snapshot.spec.colorSpec.profileId;dest.bytes.resize(std::size_t(w)*h*4);
            for(const auto layer:order) {
                auto raw=unwrap(renderer.renderRegion(snapshot,layer,{0,0,w,h},{snapshot.revision,limit,ticks}));
                auto clipped=unwrap(nativeMaskedTile(snapshot,layer,std::move(raw),0,0));over(dest,clipped);
            }
            return dest;
        };
        write(output/"zero.rgba",render(0,UINT64_MAX).bytes);
        for(unsigned phase=0;phase<3;++phase) {
            std::uint64_t end=0;for(const auto& event:*snapshot.eventIndex)
                if(std::find(phases.begin(),phases.end(),nodes->at(event.layerId).stage)-phases.begin()<=phase)++end;
            write(output/(phases[phase]+".rgba"),render(end,UINT64_MAX).bytes);
        }
        const auto final=render(UINT64_MAX,UINT64_MAX);write(output/"final.rgba",final.bytes);
        for(const auto percent:{25u,50u,75u})write(output/("replay-"+std::to_string(percent)+".rgba"),render((events.size()*percent+99)/100,UINT64_MAX).bytes);
        std::ostringstream canonical;canonical<<std::setprecision(17)<<'[';
        bool first=true;
        for(const auto& e:*snapshot.eventIndex) {
            if(!first)canonical<<',';
            first=false;
            canonical<<"{\"phase\":\""<<nodes->at(e.layerId).stage<<"\",\"objectId\":\""<<hexId(e.objectId)<<"\",\"layerId\":\""<<hexId(e.layerId)
                     <<"\",\"startUs\":"<<e.startUs<<",\"endUs\":"<<e.endUs<<",\"width\":"<<e.style.width<<",\"hardness\":"<<e.style.hardness<<",\"opacity\":"<<e.style.opacity<<",\"color\":[";
            for(unsigned c=0;c<3;++c){if(c)canonical<<',';canonical<<e.color.channels[c];}canonical<<','<<e.color.alpha<<"],\"geometry\":[";
            for(std::size_t i=0;i<e.geometry.size();++i) {
                if(i)canonical<<',';
                const auto& p=e.geometry[i];canonical<<"{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"pressure\":"<<p.pressure<<",\"timestampUs\":"<<p.timestampUs<<'}';
            }
            canonical<<"]}";
        }
        canonical<<']';const auto text=canonical.str();Bytes bytes(text.begin(),text.end());
        std::ostringstream layers;layers<<'[';first=true;
        for(const auto layer:order) {
            auto raw=unwrap(renderer.renderRegion(snapshot,layer,{0,0,w,h},{snapshot.revision}));
            auto clipped=unwrap(nativeMaskedTile(snapshot,layer,std::move(raw),0,0));std::size_t visible=0;
            for(std::size_t p=3;p<clipped.bytes.size();p+=4)if(clipped.bytes[p])++visible;
            if(!first)layers<<',';
            first=false;
            layers<<"{\"id\":\""<<hexId(layer)<<"\",\"phase\":\""<<nodes->at(layer).stage<<"\",\"nonempty\":"<<(visible?"true":"false")<<",\"visiblePixels\":"<<visible<<'}';
        }
        layers<<']';
        std::ofstream audit(output/"audit.json");audit<<"{\"declaredBaselineEvents\":"<<baselineCount<<",\"declaredCorrectionEvents\":"<<(count-baselineCount)<<",\"baselineAuthenticity\":\"not-certified-by-importer\",\"researchBudget\":{\"maxEvents\":2048,\"maxSamples\":250000,\"maxNib\":128},\"qualityStatus\":\"not-evaluated\",\"naturalProcessVerified\":false,\"nativeRenderer\":\"Drawborad ReferenceRenderer C++ PR6 portable sources\",\"sourceAssetReads\":"<<renderer.statistics().sourceAssetReads
            <<",\"phaseOrder\":[\"base\",\"form\",\"detail\"],\"phaseOrderingScope\":\"global\",\"savedReopenedVerified\":true,\"geometryColorDigest\":\""<<sha256(bytes).hex()<<"\",\"resourceKinds\":[";
        bool firstResource=true;for(const auto& [id,r]:*snapshot.resources) {(void)id;if(!firstResource)audit<<',';firstResource=false;if(r.kind!=ResourceKind::Mask)throw std::runtime_error("unexpected persisted resource");audit<<"\"Mask\"";}
        audit<<"],\"layers\":"<<layers.str()<<",\"events\":"<<text<<'}';if(!audit)throw std::runtime_error("audit write failed");
        std::cout<<"events="<<events.size()<<" samples="<<samples<<" layers="<<order.size()<<" sourceAssetReads="<<renderer.statistics().sourceAssetReads<<" savedReopened=true\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
