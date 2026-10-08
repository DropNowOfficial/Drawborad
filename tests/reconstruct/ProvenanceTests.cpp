#include "Test.h"
#include "RenderFixture.h"
#include "reconstruct/ReconstructionService.h"
#include "render/ReferenceRenderer.h"
#include "core/Commands.h"
#include <memory>
#include <stop_token>
using namespace drawborad;

namespace {
struct TestContext {
    DocumentSnapshot snapshot;
    TileData source;
    ReviewedObjects reviewed;
    ReconstructionRequest request;
    explicit TestContext(){
        snapshot=fixture::snapshot(ColorMode::RGB,Depth::U8,64,64);
        source.format=snapshot.spec.colorSpec.format;
        source.width=64;source.height=64;source.rowStride=64*4;
        source.bytes.assign(source.rowStride*source.height,191);
        reviewed.confirmedRevision=snapshot.revision;
        reviewed.canReconstruct=true;
        reviewed.masks.push_back(Bytes(64*64,255));
        request.snapshot=snapshot;
        request.reviewed=reviewed;
        request.source.pixels=&source;
        request.objectId=Id::fromNumber(2);
        request.layerId=Id::fromNumber(3);
        request.proposedEvents.push_back(fixture::stroke(snapshot,10,11,20,21));
    }
};
}

TEST("E01 accepted events commit atomically and are replayable without source pixels"){
    TestContext ctx;
    ReconstructionService service;
    auto compiled=service.compile(ctx.request);
    CHECK(compiled);
    CHECK(compiled.value().staged.events.size()==1);
    auto batch=compiled.value().staged;
    ctx.source.bytes.clear();
    auto doc=Document::fromSnapshot(ctx.snapshot); CHECK(doc);
    auto applied=commitReconstructionBatch(doc.value(),batch); CHECK(applied);
    CHECK(doc.value().snapshot().revision==ctx.snapshot.revision+1);
    ReferenceRenderer renderer;
    auto snap=doc.value().snapshot();
    EventLimit limit{snap.revision,snap.eventIndex->size(),UINT64_MAX};
    auto first=renderer.renderRegion(snap,Id::fromNumber(3),{0,0,64,64},limit);CHECK(first);
    auto noSource=snap;
    noSource.resources=std::make_shared<const ResourceMap>();
    auto second=renderer.renderRegion(noSource,Id::fromNumber(3),{0,0,64,64},limit);CHECK(second);
    CHECK(canonicalPixelHash(first.value())==canonicalPixelHash(second.value()));
    CHECK(renderer.statistics().sourceAssetReads==0);
}
TEST("E01 rejects bitmap replacement and source-handle tools"){
    for(const char* tool : {"bitmap", "source_handle", "full-residual"}){
        TestContext ctx; ctx.request.proposedEvents[0].toolId=tool;
        auto result=ReconstructionService{}.compile(ctx.request);
        CHECK(!result);
        CHECK(result.error().code==ErrorCode::UnsupportedFeature);
    }
}
TEST("E01 rejects an unreviewed object or off-mask stroke"){
    TestContext ctx;
    ctx.request.objectId=Id::fromNumber(999);
    auto invalid=ReconstructionService{}.compile(ctx.request);
    CHECK(!invalid);
    ctx.request.objectId=Id::fromNumber(2);
    ctx.request.reviewed.masks[0][11*64+10]=0;
    invalid=ReconstructionService{}.compile(ctx.request);
    CHECK(!invalid);
    CHECK(invalid.error().code==ErrorCode::InvalidInput);
}
TEST("E01 rejects stale revision and cancellation without mutation"){
    TestContext ctx;
    auto doc=Document::fromSnapshot(ctx.snapshot); CHECK(doc);
    ctx.request.reviewed.confirmedRevision=ctx.snapshot.revision+1;
    auto stale=ReconstructionService{}.compile(ctx.request);
    CHECK(!stale);CHECK(stale.error().code==ErrorCode::StaleRevision);
    ctx.request.reviewed.confirmedRevision=ctx.snapshot.revision;
    std::stop_source cancelled;cancelled.request_stop();
    auto result=ReconstructionService{}.compile(ctx.request,cancelled.get_token());
    CHECK(!result);CHECK(result.error().code==ErrorCode::Cancelled);
    CHECK(doc.value().snapshot().eventIndex->empty());
    CHECK(doc.value().snapshot().revision==ctx.snapshot.revision);
}
TEST("E01 rejects oversized grain, paths and invalid geometry"){
    TestContext ctx;
    ctx.request.proposedEvents[0].style.grain.assign(64*64,255);
    auto bad=ReconstructionService{}.compile(ctx.request);CHECK(!bad);
    ctx.request.proposedEvents[0].style.grain.assign(64,255);
    ctx.request.proposedEvents[0].geometry[0].pressure=2;
    bad=ReconstructionService{}.compile(ctx.request);CHECK(!bad);
    ctx.request.proposedEvents[0].geometry[0].pressure=1;
    ctx.request.proposedEvents[0].style.width=10000;
    bad=ReconstructionService{}.compile(ctx.request);CHECK(!bad);
}
TEST("E01 batch prevents duplicate stroke ids and invalid tools"){
    TestContext ctx;
    EventBatch batch{ctx.snapshot.revision,ctx.request.objectId,ctx.request.layerId,ctx.request.proposedEvents};
    CHECK(validateReconstructionBatch(batch));
    batch.events.push_back(batch.events.front());
    auto bad=validateReconstructionBatch(batch);
    CHECK(!bad); CHECK(bad.error().code==ErrorCode::InvalidInput);
    batch.events.pop_back();
    batch.events[0].toolId="raster-image";
    bad=validateReconstructionBatch(batch);
    CHECK(!bad); CHECK(bad.error().code==ErrorCode::UnsupportedFeature);
}
TEST("E01 canceled commit and stale commit leave the document unchanged"){
    TestContext ctx;
    auto result=ReconstructionService{}.compile(ctx.request);CHECK(result);
    auto doc=Document::fromSnapshot(ctx.snapshot);CHECK(doc);
    std::stop_source cancelled;cancelled.request_stop();
    auto rejected=commitReconstructionBatch(doc.value(),result.value().staged,cancelled.get_token());
    CHECK(!rejected);CHECK(rejected.error().code==ErrorCode::Cancelled);
    CHECK(doc.value().snapshot().eventIndex->empty());
    Command rename(RenameNode{Id::fromNumber(3),"Edited"});
    CHECK(doc.value().apply(rename,ctx.snapshot.revision));
    rejected=commitReconstructionBatch(doc.value(),result.value().staged);
    CHECK(!rejected);CHECK(rejected.error().code==ErrorCode::StaleRevision);
    CHECK(doc.value().snapshot().eventIndex->empty());
}
TEST("E01 rejects a partially invalid staged batch without committing anything"){
    TestContext ctx;
    auto result=ReconstructionService{}.compile(ctx.request);CHECK(result);
    auto batch=result.value().staged;
    auto bad=batch.events.front();bad.id=Id::fromNumber(887);bad.layerId=Id::fromNumber(999);
    batch.events.push_back(bad);
    auto doc=Document::fromSnapshot(ctx.snapshot);CHECK(doc);
    auto committed=commitReconstructionBatch(doc.value(),batch);
    CHECK(!committed);
    CHECK(doc.value().snapshot().eventIndex->empty());
    CHECK(doc.value().snapshot().revision==ctx.snapshot.revision);
}

TEST("E01 allows a reconstructed layer inside its object group") {
    TestContext ctx;
    auto nodes=std::make_shared<NodeMap>(*ctx.snapshot.nodes);
    const auto objectId=Id::fromNumber(2);
    const auto layerId=Id::fromNumber(3);
    const auto groupId=Id::fromNumber(4);
    LayerNode group;
    group.id=groupId;
    group.parent=objectId;
    group.objectId=objectId;
    group.kind=NodeKind::Group;
    group.bounds={0,0,64,64};
    group.children={layerId};
    (*nodes)[objectId].children={groupId};
    (*nodes)[layerId].parent=groupId;
    nodes->emplace(groupId,group);
    ctx.snapshot.nodes=nodes;
    ctx.request.snapshot=ctx.snapshot;
    auto compiled=ReconstructionService{}.compile(ctx.request);
    CHECK(compiled);
    auto doc=Document::fromSnapshot(ctx.snapshot);CHECK(doc);
    CHECK(commitReconstructionBatch(doc.value(),compiled.value().staged));
}

TEST("E01 rejects out-of-order staged events before publication") {
    TestContext ctx;
    auto first=ctx.request.proposedEvents.front();
    auto second=first;
    second.id=Id::fromNumber(11);
    second.startUs=50;
    second.endUs=150;
    second.geometry[0].timestampUs=50;
    second.geometry[1].timestampUs=150;
    ctx.request.proposedEvents.push_back(second);
    auto staged=ReconstructionService{}.compile(ctx.request);
    CHECK(!staged);
    CHECK(staged.error().messageKey=="reconstruct.event-order");
}
