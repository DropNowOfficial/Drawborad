#include "Test.h"
#include "RenderFixture.h"
#include "reconstruct/Refinement.h"
#include "render/ReferenceRenderer.h"
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
    RefinementRequest request;
    explicit Context(ColorMode mode=ColorMode::RGB,Depth depth=Depth::U8){
        snapshot=fixture::snapshot(mode,depth,64,64);
        source.format=snapshot.spec.colorSpec.format;
        source.profileId=snapshot.spec.colorSpec.profileId;
        source.width=64;source.height=64;
        const unsigned sample=depth==Depth::U16?2u:1u;
        const unsigned channels=source.format.channelCount();
        source.rowStride=64u*channels*sample;
        source.bytes.assign(std::size_t(source.rowStride)*64,0);
        mask.assign(64*64,0);
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x){
            mask[y*64+x]=255;
            const auto at=std::size_t(y)*source.rowStride+x*channels*sample;
            for(unsigned ch=0;ch<channels;++ch){
                const std::uint8_t value=ch==channels-1?255:
                    (mode==ColorMode::CMYK ? (ch==0?128u:ch==3?30u:10u) :
                     (ch==0?220u:ch==1?24u:18u));
                if(sample==2){source.bytes[at+ch*2]=value;source.bytes[at+ch*2+1]=value;}
                else source.bytes[at+ch]=value;
            }
        }
        request.region.snapshot=snapshot;
        request.region.objectId=Id::fromNumber(2);
        request.region.layerId=Id::fromNumber(3);
        request.region.bounds={0,0,64,64};
        request.region.reviewedMask=mask;
        request.region.source=&source;
        request.region.seed=20261008;
        request.painted=snapshot;
        request.budget.maxAdditionalEvents=72;
        request.budget.maxAdditionalSamples=160;
        request.budget.gridStep=4;
        request.budget.maxStrokeWidth=2;
    }
};
void noOutsideAlpha(const Context& ctx,const EventBatch& batch){
    auto snap=fixture::withEvents(ctx.snapshot,batch.events);
    ReferenceRenderer renderer;
    auto tile=renderer.renderRegion(snap,ctx.request.region.layerId,{0,0,64,64},
      {snap.revision,snap.eventIndex->size(),UINT64_MAX});
    CHECK(tile);
    const unsigned stride=tile.value().format.channelCount();
    const unsigned sample=tile.value().format.depth==Depth::U16?2u:1u;
    for(std::size_t y=0;y<64;++y)for(std::size_t x=0;x<64;++x){
        if(ctx.mask[y*64+x]!=0)continue;
        const auto at=y*tile.value().rowStride+(x*stride+stride-1)*sample;
        CHECK(tile.value().bytes[at]==0);
        if(sample==2)CHECK(tile.value().bytes[at+1]==0);
    }
}
}
TEST("E03 progress measures actual alpha and color error, not a fake percent"){
    Context c;
    const auto progress=measureRegionProgress(c.request);
    CHECK(progress);
    CHECK(progress.value().status==QualityStatus::NeedsReview);
    CHECK(progress.value().alphaMae>0.25);
    CHECK(progress.value().colorMae>0.10);
    CHECK(progress.value().remainingPixels>0);
    CHECK(progress.value().evaluatedPixels==48ull*48ull);
}
TEST("E03 refinement emits deterministic local pigment events with bounded samples"){
    Context c;
    const auto a=refineRegion(c.request);CHECK(a);
    const auto b=refineRegion(c.request);CHECK(b);
    CHECK(a.value().events==b.value().events);
    CHECK(!a.value().events.empty());
    CHECK(a.value().events.size()<=c.request.budget.maxAdditionalEvents);
    std::size_t samples=0;
    for(const auto& e:a.value().events){
        CHECK(e.toolId=="marker" || e.toolId=="colored-pencil" || e.toolId=="pencil");
        CHECK(e.parentRevision==c.snapshot.revision);
        CHECK(e.objectId==c.request.region.objectId);
        CHECK(e.layerId==c.request.region.layerId);
        CHECK(e.style.width<=c.request.budget.maxStrokeWidth);
        CHECK(e.geometry.size()>=2);
        CHECK(e.geometry.size()<=16);
        samples+=e.geometry.size();
    }
    CHECK(samples<=c.request.budget.maxAdditionalSamples);
}
TEST("E03 painted result measurably reduces local alpha and color errors"){
    Context c;
    const auto before=measureRegionProgress(c.request);CHECK(before);
    const auto events=refineRegion(c.request);CHECK(events);
    c.request.painted=fixture::withEvents(c.snapshot,events.value().events);
    const auto after=measureRegionProgress(c.request);CHECK(after);
    CHECK(after.value().alphaMae<before.value().alphaMae);
    CHECK(after.value().colorMae<before.value().colorMae);
}
TEST("E03 transparent holes and other objects receive no pigment"){
    Context c;
    for(unsigned y=25;y<40;++y)for(unsigned x=25;x<40;++x){
       c.mask[y*64+x]=0;
       const auto at=y*c.source.rowStride+x*4;
       c.source.bytes[at+3]=0;
    }
    auto before=c.snapshot;
    const auto out=refineRegion(c.request);CHECK(out);
    CHECK(c.snapshot.sameContent(before));
    noOutsideAlpha(c,out.value());
}
TEST("E03 no event budget reports NeedsReview instead of complete"){
    Context c;
    c.request.budget.maxAdditionalEvents=0;
    const auto progress=measureRegionProgress(c.request);CHECK(progress);
    CHECK(progress.value().status==QualityStatus::NeedsReview);
    const auto out=refineRegion(c.request);
    CHECK(!out);
    CHECK(out.error().code==ErrorCode::BudgetExceeded);
    CHECK(out.error().messageKey=="reconstruct.needs-review");
}
TEST("E03 cancellation and stale revisions never stage work"){
    Context c;
    std::stop_source cancel;cancel.request_stop();
    auto result=refineRegion(c.request,cancel.get_token());
    CHECK(!result);CHECK(result.error().code==ErrorCode::Cancelled);
    c.request.painted.revision++;
    result=refineRegion(c.request);
    CHECK(!result);CHECK(result.error().code==ErrorCode::StaleRevision);
}
TEST("E03 keeps CMYKA16 pigment native and preserves ICC identity"){
    Context c(ColorMode::CMYK,Depth::U16);
    const auto out=refineRegion(c.request);CHECK(out);
    CHECK(!out.value().events.empty());
    for(const auto& e:out.value().events){
        CHECK(e.color.activeChannels==4);
        CHECK(e.color.profileId==c.source.profileId);
        for(int ch=0;ch<4;++ch)CHECK(e.color.channels[ch]>=0 && e.color.channels[ch]<=1);
    }
    noOutsideAlpha(c,out.value());
}
TEST("E03 staged strokes replay identically after source pixels are removed"){
    Context c;
    const auto out=refineRegion(c.request);CHECK(out);
    auto snap=fixture::withEvents(c.snapshot,out.value().events);
    ReferenceRenderer r;
    const EventLimit limit{snap.revision,snap.eventIndex->size(),UINT64_MAX};
    auto a=r.renderRegion(snap,c.request.region.layerId,{0,0,64,64},limit);CHECK(a);
    c.source.bytes.clear();
    auto b=r.renderRegion(snap,c.request.region.layerId,{0,0,64,64},limit);CHECK(b);
    CHECK(canonicalPixelHash(a.value())==canonicalPixelHash(b.value()));
    CHECK(r.statistics().sourceAssetReads==0);
}
TEST("E03 fully transparent target is converged and does not synthesize fake strokes"){
    Context c;
    std::fill(c.source.bytes.begin(),c.source.bytes.end(),std::uint8_t{0});
    auto progress=measureRegionProgress(c.request);CHECK(progress);
    CHECK(progress.value().status==QualityStatus::Converged);
    auto result=refineRegion(c.request);CHECK(result);
    CHECK(result.value().events.empty());
}
TEST("E03 prioritizes a localized red fine fold over zero-error background"){
    Context c;
    std::fill(c.source.bytes.begin(),c.source.bytes.end(),std::uint8_t{0});
    for(int y=28;y<36;++y)for(int x=30;x<35;++x){
        const auto at=std::size_t(y)*c.source.rowStride+x*4;
        c.source.bytes[at]=255;c.source.bytes[at+3]=255;
    }
    const auto out=refineRegion(c.request);CHECK(out);
    CHECK(!out.value().events.empty());
    const auto& e=out.value().events.front();
    CHECK(e.geometry.front().x>=27 && e.geometry.front().x<=38);
    CHECK(e.geometry.front().y>=25 && e.geometry.front().y<=39);
}
TEST("E03 feathered boundary reports remaining work without ink outside reviewed mask") {
    Context c;
    for(unsigned y=8;y<56;++y){ c.mask[y*64+8]=127;c.mask[y*64+55]=127; }
    const auto staged=refineRegion(c.request);CHECK(staged);
    noOutsideAlpha(c,staged.value());
    c.request.painted=fixture::withEvents(c.snapshot,staged.value().events);
    const auto progress=measureRegionProgress(c.request);CHECK(progress);
    CHECK(progress.value().status==QualityStatus::NeedsReview);
    CHECK(progress.value().remainingPixels>0);
}
TEST("E03 metric processes ROI across multiple 512 pixel renderer tiles") {
    const auto snapshot=fixture::snapshot(ColorMode::RGB,Depth::U8,576,576);
    TileData source;
    source.format=snapshot.spec.colorSpec.format;
    source.profileId=snapshot.spec.colorSpec.profileId;
    source.width=576;source.height=576;source.rowStride=576u*4u;
    source.bytes.assign(source.rowStride*source.height,0);
    Bytes mask(576u*576u,0);
    for(unsigned y=16;y<560;++y)for(unsigned x=16;x<560;++x){
        mask[y*576u+x]=255;
        const auto at=y*source.rowStride+x*4u;
        source.bytes[at]=200;source.bytes[at+3]=255;
    }
    RefinementRequest request;
    request.region.snapshot=snapshot;
    request.region.objectId=Id::fromNumber(2);
    request.region.layerId=Id::fromNumber(3);
    request.region.bounds={0,0,576,576};
    request.region.reviewedMask=mask;
    request.region.source=&source;
    request.painted=snapshot;
    request.budget.gridStep=12;
    const auto result=measureRegionProgress(request);CHECK(result);
    CHECK(result.value().status==QualityStatus::NeedsReview);
    CHECK(result.value().evaluatedPixels==544ull*544ull);
    CHECK(result.value().alphaMae>.95);
}
