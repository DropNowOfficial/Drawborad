#include "Test.h"
#include "RenderFixture.h"
#include "reconstruct/CriticalRegions.h"
#include "text/TextCandidateService.h"
#include <algorithm>
#include <memory>
#include <stop_token>
using namespace drawborad;
namespace {
struct Context {
    DocumentSnapshot snapshot;
    TileData source;
    CriticalRegionRequest request;
    Context(ColorMode mode=ColorMode::RGB,Depth depth=Depth::U8,
            std::uint32_t side=64) {
        auto base=fixture::snapshot(mode,depth,side,side);
        snapshot=fixture::withEvents(base,{fixture::stroke(base,14,14,side-14,side-14)});
        ReferenceRenderer renderer;
        auto image=renderer.renderRegion(snapshot,Id::fromNumber(3),
                                         {0,0,side,side},
                                         {snapshot.revision,1,UINT64_MAX});
        if(!image)throw std::runtime_error("fixture render");
        source=image.value();
        request.reconstructed=snapshot;
        request.source=&source;
        request.layerId=Id::fromNumber(3);
        request.expectedRevision=snapshot.revision;
        request.regions.push_back({{8,8,side-16,side-16},CriticalKind::Lettering,"word"});
    }
    void sourcePixel(std::uint32_t x,std::uint32_t y,std::uint8_t r,std::uint8_t a=255) {
        const unsigned bytes=source.format.depth==Depth::U16?2:1;
        const auto channels=source.format.channelCount();
        const auto off=std::size_t(y)*source.rowStride+std::size_t(x)*channels*bytes;
        source.bytes[off]=r;
        if(bytes==2)source.bytes[off+1]=r;
        const auto alpha=off+(channels-1)*bytes;
        source.bytes[alpha]=a;
        if(bytes==2)source.bytes[alpha+1]=a;
    }
};
}
TEST("E04 identical rendered glyph has independently passing ROI score") {
    Context c;
    auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(result.value().accepted);
    CHECK(result.value().regions.size()==1);
    CHECK(result.value().regions[0].passed);
    CHECK(result.value().regions[0].colorMae==0);
    CHECK(result.value().regions[0].alphaMae==0);
    CHECK(result.value().regions[0].edgeMismatch==0);
    CHECK(!result.value().editableTextClaimed);
}
TEST("E04 changed tiny logo rejects even when huge background matches") {
    Context c;
    c.request.regions={{{8,45,12,12},CriticalKind::Logo,"corner-logo",0.03,0.03,0.06}};
    for(std::uint32_t y=47;y<51;++y)for(std::uint32_t x=10;x<15;++x)c.sourcePixel(x,y,255);
    auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(!result.value().accepted);
    CHECK(result.value().regions.size()==1);
    CHECK(!result.value().regions[0].passed);
    CHECK(result.value().regions[0].alphaMae>0.02);
}
TEST("E04 logo edge mismatch independently rejects shape substitution") {
    Context c;
    c.request.regions={{{8,45,12,12},CriticalKind::Logo,"logo-edges",1.0,1.0,0.01}};
    for(std::uint32_t y=47;y<51;++y)for(std::uint32_t x=10;x<15;++x)c.sourcePixel(x,y,255);
    auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(!result.value().accepted);
    CHECK(result.value().regions[0].colorMae<=1);
    CHECK(result.value().regions[0].alphaMae<=1);
    CHECK(result.value().regions[0].edgeMismatch>0.01);
}
TEST("E04 one failing lettering ROI cannot be averaged with passing logo ROI") {
    Context c;
    c.request.regions={{{8,45,12,12},CriticalKind::Lettering,"bad-letter",0.01,0.01,0.01},
                       {{22,22,16,16},CriticalKind::Logo,"good-logo",0.03,0.03,0.03}};
    for(std::uint32_t y=47;y<51;++y)for(std::uint32_t x=10;x<15;++x)c.sourcePixel(x,y,255);
    auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(!result.value().accepted);
    CHECK(result.value().regions.size()==2);
    CHECK(!result.value().regions[0].passed);
    CHECK(result.value().regions[1].passed);
}
TEST("E04 changes outside selected ROI do not modify its score") {
    Context c;
    const auto original=checkCriticalRegions(c.request);CHECK(original);
    c.request.regions={{{22,22,16,16},CriticalKind::Lettering,"center"}};
    c.sourcePixel(1,1,255);
    const auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(result.value().accepted);
    CHECK(result.value().regions[0].alphaMae==0);
}
TEST("E04 native CMYK16 ROI compares channels without silent RGB conversion") {
    Context c(ColorMode::CMYK,Depth::U16);
    auto result=checkCriticalRegions(c.request);
    CHECK(result);CHECK(result.value().accepted);
    c.request.regions[0].maxColorMae=0.00001;
    c.sourcePixel(26,26,0,255);
    result=checkCriticalRegions(c.request);CHECK(result);
    CHECK(!result.value().accepted);
    CHECK(result.value().regions[0].colorMae>0);
}
TEST("E04 profile mismatch and invalid source stride are errors") {
    Context c;
    c.source.profileId.bytes[0]^=1;
    auto result=checkCriticalRegions(c.request);
    CHECK(!result);
    c.source.profileId=c.snapshot.spec.colorSpec.profileId;
    c.source.rowStride=1;
    result=checkCriticalRegions(c.request);
    CHECK(!result);
}
TEST("E04 rejects stale snapshots invalid bounds and empty critical region list") {
    Context c;
    ++c.request.expectedRevision;
    auto result=checkCriticalRegions(c.request);
    CHECK(!result);CHECK(result.error().code==ErrorCode::StaleRevision);
    c.request.expectedRevision=c.snapshot.revision;
    c.request.regions[0].bounds={60,60,16,16};
    result=checkCriticalRegions(c.request);CHECK(!result);
    c.request.regions.clear();
    result=checkCriticalRegions(c.request);CHECK(!result);
}
TEST("E04 cancel does not publish an optimistic passing report") {
    Context c;
    std::stop_source cancelled;cancelled.request_stop();
    auto result=checkCriticalRegions(c.request,cancelled.get_token());
    CHECK(!result);CHECK(result.error().code==ErrorCode::Cancelled);
}
TEST("E04 optional text recognition never starts without explicit consent") {
    Context c;
    int calls=0;
    OfflineTextProvider provider=[&](const TextCandidateRequest&,std::stop_token)
        ->Result<std::vector<TextCandidate>> {++calls;return std::vector<TextCandidate>{{"OCR",.7,"eng"}};};
    auto result=suggestText({false,{4,4,32,32},&c.source},provider);
    CHECK(!result);CHECK(calls==0);
    CHECK(result.error().code==ErrorCode::ConversionConsentRequired);
}
TEST("E04 no installed local OCR returns explicit unsupported") {
    Context c;
    auto result=suggestText({true,{4,4,32,32},&c.source});
    CHECK(!result);CHECK(result.error().code==ErrorCode::UnsupportedFeature);
}
TEST("E04 explicit local text candidate is advisory and does not substitute fonts") {
    Context c;
    const auto before=c.snapshot;
    auto original=c.source.bytes;
    int calls=0;
    OfflineTextProvider provider=[&](const TextCandidateRequest&,std::stop_token)
        ->Result<std::vector<TextCandidate>> {
            ++calls;return std::vector<TextCandidate>{{"\xe4\xb8\xad\xe6\x96\x87",0.85,"chi_sim"},
                                                      {"BRAND",0.72,"eng"}};
        };
    auto result=suggestText({true,{4,4,32,32},&c.source},provider);
    CHECK(result);CHECK(calls==1);
    CHECK(result.value().suggestions.size()==2);
    CHECK(result.value().suggestions[0].suggestedText=="\xe4\xb8\xad\xe6\x96\x87");
    CHECK(!result.value().fontSubstituted);
    CHECK(!result.value().editableTextLayerCreated);
    CHECK(!result.value().sourceModified);
    CHECK(c.snapshot.sameContent(before));CHECK(c.source.bytes==original);
}
TEST("E04 optional recognizer rejects invalid confidence and no-source consent") {
    Context c;
    OfflineTextProvider bad=[](const TextCandidateRequest&,std::stop_token)
        ->Result<std::vector<TextCandidate>> {return std::vector<TextCandidate>{{"wrong",1.5,"eng"}};};
    auto result=suggestText({true,{4,4,32,32},&c.source},bad);CHECK(!result);
    result=suggestText({true,{4,4,32,32},nullptr},bad);CHECK(!result);
}
