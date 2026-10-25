#include "Test.h"
#include "RenderFixture.h"
#include "reconstruct/Planning.h"
#include "render/ReferenceRenderer.h"
#include "core/Commands.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stop_token>
using namespace drawborad;
namespace {
struct Context {
    DocumentSnapshot snapshot;
    TileData source;
    Bytes mask;
    PlanningRegion region;
    explicit Context(ColorMode mode=ColorMode::RGB,Depth depth=Depth::U8){
        snapshot=fixture::snapshot(mode,depth,64,64);
        source.format=snapshot.spec.colorSpec.format;
        source.profileId=snapshot.spec.colorSpec.profileId;
        source.width=64; source.height=64;
        const auto bytesPer=depth==Depth::U16?2u:1u;
        source.rowStride=64u*source.format.channelCount()*bytesPer;
        source.bytes.assign(std::size_t(source.rowStride)*64u,0);
        for(std::uint32_t y=0;y<64;++y)for(std::uint32_t x=0;x<64;++x){
            for(std::uint32_t c=0;c<source.format.channelCount();++c){
                const auto value=std::uint8_t(c==0?x*4:c==1?y*4:c==2?120:255);
                const auto at=std::size_t(y)*source.rowStride+(x*source.format.channelCount()+c)*bytesPer;
                if(bytesPer==2){source.bytes[at]=value;source.bytes[at+1]=value;}
                else source.bytes[at]=value;
            }
        }
        mask.assign(64u*64u,0);
        for(std::uint32_t y=9;y<55;++y)for(std::uint32_t x=7;x<57;++x)mask[y*64+x]=255;
        region.snapshot=snapshot;region.objectId=Id::fromNumber(2);region.layerId=Id::fromNumber(3);
        region.bounds={0,0,64,64};region.reviewedMask=mask;region.source=&source;region.seed=314159;
    }
};
void assertNoLeak(const EventBatch& batch,const Context& c){
    auto snap=fixture::withEvents(c.snapshot,batch.events);
    ReferenceRenderer renderer;
    auto tile=renderer.renderRegion(snap,c.region.layerId,{0,0,64,64},
                                  {snap.revision,batch.events.size(),UINT64_MAX});
    CHECK(tile);
    const auto& result=tile.value();
    for(std::uint64_t y=0;y<64;++y)for(std::uint64_t x=0;x<64;++x){
        if(c.mask[y*64+x])continue;
        const auto a=(y*result.rowStride+x*result.format.channelCount()*(result.format.depth==Depth::U16?2:1))+
                     (result.format.channelCount()-1)*(result.format.depth==Depth::U16?2:1);
        CHECK(result.bytes[std::size_t(a)]==0);
        if(result.format.depth==Depth::U16)CHECK(result.bytes[std::size_t(a+1)]==0);
    }
}
}
TEST("E02 contour plan is continuous, closed, deterministic, and independent of source pixels"){
    Context c;
    auto one=planContours(c.region); CHECK(one);
    auto two=planContours(c.region); CHECK(two);
    CHECK(one.value().events==two.value().events);
    CHECK(one.value().events.size()<=4);
    CHECK(!one.value().events.empty());
    for(const auto& e:one.value().events){
        CHECK(e.toolId=="guide");CHECK(e.geometry.size()>=4);
        CHECK(std::hypot(e.geometry.front().x-e.geometry.back().x,
                         e.geometry.front().y-e.geometry.back().y)<.01);
        for(std::size_t i=1;i<e.geometry.size();++i)
            CHECK(std::hypot(e.geometry[i].x-e.geometry[i-1].x,
                             e.geometry[i].y-e.geometry[i-1].y)<65.0);
    }
    CHECK(validateReconstructionBatch(one.value()));
    c.source.bytes.clear();
    CHECK(planContours(c.region).value().events==one.value().events);
    assertNoLeak(one.value(),c);
}
TEST("E02 ring contours preserve the unpainted transparent hole"){
    Context c;
    for(int y=24;y<40;++y)for(int x=24;x<40;++x)c.mask[y*64+x]=0;
    auto outlines=planContours(c.region);CHECK(outlines);
    CHECK(outlines.value().events.size()>=2);
    assertNoLeak(outlines.value(),c);
}
TEST("E02 base pigment uses long connected strokes and never draws outside review"){
    Context c;
    StyleSettings style;
    auto pigment=planBasePigment(c.region,style);CHECK(pigment);
    CHECK(!pigment.value().events.empty());
    CHECK(pigment.value().events.size()<220);
    for(const auto& e:pigment.value().events){
        CHECK(e.toolId=="marker");CHECK(e.geometry.size()>=2);
        CHECK(std::hypot(e.geometry[0].x-e.geometry.back().x,
                         e.geometry[0].y-e.geometry.back().y)>=3);
    }
    CHECK(validateReconstructionBatch(pigment.value()));
    assertNoLeak(pigment.value(),c);
}
TEST("E02 persisted mask clipping covers frame edges thin regions and transparent holes"){
    Context c;c.mask.assign(4096,255);
    for(unsigned y=20;y<44;++y)for(unsigned x=20;x<44;++x)c.mask[y*64+x]=0;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<5;++x)c.mask[y*64+x]=0;
    for(unsigned y=0;y<64;++y)c.mask[y*64+63]=0; // Trailing empty run after a visible span.
    c.source.bytes.resize(c.source.bytes.size()+4); // Valid extra buffer capacity; no out-of-range read can mask the geometry defect.
    for(unsigned y=8;y<56;++y)c.mask[y*64+1]=255; // One-pixel-wide visible fragment.
    c.mask[2*64+3]=255; // Visible island between the ordinary sweep rows.
    auto resources=std::make_shared<ResourceMap>(*c.snapshot.resources);
    Resource mask{sha256(c.mask),ResourceKind::Mask,std::make_shared<const Bytes>(c.mask),{}};resources->emplace(mask.id,mask);c.snapshot.resources=resources;
    auto nodes=std::make_shared<NodeMap>(*c.snapshot.nodes);nodes->at(c.region.objectId).maskId=mask.id;c.snapshot.nodes=nodes;
    c.region.snapshot=c.snapshot;c.region.reviewedMask=c.mask;c.region.clipToReviewedMask=true;
    // A four-pixel round nib at two-pixel spacing covers the stamp
    // rasterizer's half-pixel falloff without scalloped translucent edges.
    const auto paint=planBasePigment(c.region,StyleSettings{"marker",4,2,1,0});CHECK(paint);
    for(const auto& event:paint.value().events)for(const auto& point:event.geometry)CHECK(point.x>=0 && point.y>=0 && point.x<64 && point.y<64);
    auto snapshot=fixture::withEvents(c.snapshot,paint.value().events);ReferenceRenderer renderer;
    const auto raw=renderer.renderRegion(snapshot,c.region.layerId,{0,0,64,64},{snapshot.revision});CHECK(raw);
    const auto clipped=nativeMaskedTile(snapshot,c.region.layerId,raw.value(),0,0);CHECK(clipped);
    unsigned missing=0;
    for(unsigned p=0;p<4096;++p)if(clipped.value().bytes[p*4+3]!=c.mask[p]){
        if(missing<8)std::cout<<"mask clipping mismatch x="<<p%64<<" y="<<p/64<<" alpha="<<unsigned(clipped.value().bytes[p*4+3])<<" expected="<<unsigned(c.mask[p])<<'\n';
        ++missing;
    }
    CHECK(missing==0);
    CHECK(paint.value().events.size()<100);
    c.region.snapshot.resources=std::make_shared<const ResourceMap>();CHECK(!planBasePigment(c.region,StyleSettings{}));
}
TEST("E02 base follows source gradient tangents and samples native colors"){
    Context c;
    StyleSettings style;style.spacing=6;
    for(std::size_t y=0;y<64;++y)for(std::size_t x=0;x<64;++x){
        const auto at=y*c.source.rowStride+x*4;
        c.source.bytes[at]=std::uint8_t(x*4);
        c.source.bytes[at+1]=std::uint8_t(x*4);
        c.source.bytes[at+2]=std::uint8_t(x*4);
    }
    auto paint=planBasePigment(c.region,style);CHECK(paint);
    std::size_t vertical=0,horizontal=0;
    for(const auto& e:paint.value().events){
        const auto dx=std::abs(e.geometry.back().x-e.geometry.front().x);
        const auto dy=std::abs(e.geometry.back().y-e.geometry.front().y);
        if(dx>dy)++horizontal;else ++vertical;
        CHECK(e.color.profileId==c.snapshot.spec.colorSpec.profileId);
        CHECK(e.color.channels[0]>=0.0 && e.color.channels[0]<=1.0);
    }
    CHECK(vertical>horizontal);
}
TEST("E02 CMYKA16 pigment preserves native channels and profile"){
    Context c(ColorMode::CMYK,Depth::U16);
    auto pigment=planBasePigment(c.region,StyleSettings{});CHECK(pigment);
    CHECK(!pigment.value().events.empty());
    for(const auto& e:pigment.value().events){
        CHECK(e.color.activeChannels==4);
        CHECK(e.color.profileId==c.snapshot.spec.colorSpec.profileId);
        CHECK(e.color.alpha>=0.0 && e.color.alpha<=1.0);
    }
    assertNoLeak(pigment.value(),c);
}
TEST("E02 adaptive pigment follows source color boundaries with coherent bounded strokes"){
    Context c;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const auto at=std::size_t(y*64+x)*4;c.source.bytes[at]=x<32?std::uint8_t(20):std::uint8_t(220);
        c.source.bytes[at+1]=std::uint8_t(y*4);c.source.bytes[at+2]=100;
    }
    StyleSettings style;style.maxColorVariation=.06;
    const auto generated=planBasePigment(c.region,style);CHECK(generated);
    CHECK(generated.value().events==planBasePigment(c.region,style).value().events);
    CHECK(generated.value().events.size()<80);
    for(const auto& event:generated.value().events){
        CHECK(std::hypot(event.geometry.back().x-event.geometry.front().x,event.geometry.back().y-event.geometry.front().y)>=3);
        CHECK(std::abs(event.color.channels[0]-20.0/255)<.001 || std::abs(event.color.channels[0]-220.0/255)<.001);
        for(const auto& point:event.geometry)CHECK((point.x<32)==(event.color.channels[0]<.5));
    }
    assertNoLeak(generated.value(),c);
    style.maxColorVariation=std::numeric_limits<double>::quiet_NaN();CHECK(!planBasePigment(c.region,style));
    style.maxColorVariation=.06;c.region.maxEvents=1;CHECK(!planBasePigment(c.region,style));
}
TEST("E02 schedule orders contour before base within each region and stays deterministic"){
    Context c;
    auto contour=planContours(c.region);CHECK(contour);
    auto base=planBasePigment(c.region,StyleSettings{});CHECK(base);
    EventBatch in=contour.value();
    in.events.insert(in.events.end(),base.value().events.begin(),base.value().events.end());
    SchedulePolicy policy;policy.seed=2026;policy.regionOrder={{0,0,64,64}};
    auto output=scheduleStrokes(in,policy);CHECK(output);
    CHECK(validateReconstructionBatch(output.value()));
    CHECK(output.value().events.size()==in.events.size());
    CHECK(output.value().events==scheduleStrokes(in,policy).value().events);
    bool painting=false;
    for(const auto& e:output.value().events){
        if(e.toolId!="guide")painting=true;
        if(painting)CHECK(e.toolId!="guide");
    }
    assertNoLeak(output.value(),c);
}
TEST("E02 schedule preserves region locality and unique identities"){
    Context c;
    EventBatch input;input.inputRevision=c.snapshot.revision;
    input.objectId=c.region.objectId;input.layerId=c.region.layerId;
    for(int i=0;i<6;++i){
        auto event=fixture::stroke(c.snapshot,i%2==0?10:40,10+i%2*30,
                                   i%2==0?12:42,11+i%2*30,500+i);
        event.toolId=i%3==0?"guide":"marker";
        input.events.push_back(std::move(event));
    }
    SchedulePolicy policy;
    policy.regionOrder={{0,0,30,30},{32,32,30,30}};
    auto result=scheduleStrokes(input,policy);CHECK(result);
    CHECK(validateReconstructionBatch(result.value()));
    int transitions=0,lastRegion=-1;
    for(const auto& event:result.value().events){
        const int region=event.geometry[0].x<30?0:1;
        if(lastRegion!=-1 && region!=lastRegion)++transitions;
        lastRegion=region;
    }
    CHECK(transitions<=1);
}
TEST("E02 input budget, mask, empty safe interior and cancellation are explicit"){
    Context c;
    c.region.maxEvents=0;
    auto result=planContours(c.region);CHECK(!result);
    c.region.maxEvents=4096;c.region.reviewedMask={};
    result=planContours(c.region);CHECK(!result);
    c.region.reviewedMask=c.mask;
    c.mask.assign(c.mask.size(),0);
    result=planContours(c.region);CHECK(!result);
    auto pigment=planBasePigment(c.region,StyleSettings{});CHECK(!pigment);
    c.mask.assign(c.mask.size(),255);
    std::stop_source cancelled;cancelled.request_stop();c.region.stop=cancelled.get_token();
    result=planContours(c.region);CHECK(!result);CHECK(result.error().code==ErrorCode::Cancelled);
    pigment=planBasePigment(c.region,StyleSettings{});CHECK(!pigment);
    CHECK(pigment.error().code==ErrorCode::Cancelled);
}
TEST("E02 scheduler result can be staged with E01 and replay without source"){
    Context c;
    auto outlines=planContours(c.region);CHECK(outlines);
    auto bases=planBasePigment(c.region,StyleSettings{});CHECK(bases);
    EventBatch batch=outlines.value();
    batch.events.insert(batch.events.end(),bases.value().events.begin(),bases.value().events.end());
    auto planned=scheduleStrokes(batch,SchedulePolicy{});CHECK(planned);
    ReviewedObjects reviewed;reviewed.canReconstruct=true;reviewed.confirmedRevision=c.snapshot.revision;
    reviewed.masks={c.mask};
    ReconstructionRequest req;req.snapshot=c.snapshot;req.reviewed=reviewed;
    req.source={&c.source};req.objectId=c.region.objectId;req.layerId=c.region.layerId;
    req.proposedEvents=planned.value().events;
    req.budget.maxStrokeWidth=16;
    auto compiled=ReconstructionService{}.compile(req);CHECK(compiled);
    c.source.bytes.clear();
    auto doc=Document::fromSnapshot(c.snapshot);CHECK(doc);
    CHECK(commitReconstructionBatch(doc.value(),compiled.value().staged));
    auto snapshot=doc.value().snapshot();
    ReferenceRenderer renderer;
    auto tile=renderer.renderRegion(snapshot,c.region.layerId,{0,0,64,64},
                                    {snapshot.revision,snapshot.eventIndex->size(),UINT64_MAX});
    CHECK(tile);
    CHECK(renderer.statistics().sourceAssetReads==0);
}

TEST("E02 scheduler rejects a stroke whose geometry leaves its assigned region") {
    Context c;
    EventBatch input;
    input.inputRevision=c.snapshot.revision;
    input.objectId=c.region.objectId;
    input.layerId=c.region.layerId;
    input.events.push_back(fixture::stroke(c.snapshot,10,10,45,45,700));
    SchedulePolicy policy;
    policy.regionOrder={{0,0,30,30},{32,32,30,30}};
    const auto result=scheduleStrokes(input,policy);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::InvalidInput);
    CHECK(result.error().messageKey=="reconstruct.schedule-region");
}

TEST("E02 base stroke sampling adapts to the event sample budget") {
    Context c;
    c.region.maxSamples=65;
    auto planned=planBasePigment(c.region,StyleSettings{});
    CHECK(planned);
    std::size_t count=0;
    for(const auto& e:planned.value().events)count+=e.geometry.size();
    CHECK(count<=65);
    CHECK(planned.value().events.size()>=5);
    assertNoLeak(planned.value(),c);
}
