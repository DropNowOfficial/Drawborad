#include "Test.h"
#include "segmentation/ProposalEngine.h"
#include <algorithm>
using namespace drawborad;
TEST("M03 projected masks preserve source dimensions and explicit unassigned region"){AnalysisRequest r;r.sourceSize={16,8};r.model.taskId="sam21.image.v1";r.model.sha256.bytes[0]=9;r.prompts.boxes.push_back({2,2,6,6});ProposalEngine e;auto p=e.propose(r);CHECK(p);auto m=projectMasks(p.value(),{{16,8},true});CHECK(m);CHECK(m.value().dimensions==r.sourceSize);CHECK(m.value().unassigned.size()==128);CHECK(std::count(m.value().unassigned.begin(),m.value().unassigned.end(),255)>0);}

TEST("M03 projection honors analysis transform and source alpha without mutating source"){
    AnalysisRequest r;
    r.sourceSize={8,8};
    r.model.taskId="sam21.image.v1";
    r.model.sha256.bytes[0]=7;
    r.view.sourceTransform.affine={2,0,0,2,0,0};
    Bytes alpha(64,255);
    alpha[std::size_t(2*8+2)]=0;
    r.view.sourceAlpha={alpha};
    TileData source;
    source.width=8; source.height=8; source.format={ColorMode::RGB,Depth::U8,true}; source.rowStride=32; source.bytes.assign(256,17);
    r.view.rgbTiles.push_back(source);
    const auto sourceBefore=r.view.rgbTiles[0].bytes;
    r.prompts.boxes.push_back({1,1,4,4});
    ProposalEngine engine;
    auto proposals=engine.propose(r);
    CHECK(proposals);
    CHECK(r.view.rgbTiles[0].bytes==sourceBefore);
    auto masks=projectMasks(proposals.value(),{{8,8},true});
    CHECK(masks);
    CHECK(masks.value().fullResolutionMasks.size()==1);
    const auto& m=masks.value().fullResolutionMasks[0];
    CHECK(m[std::size_t(1*8+1)]==255);
    CHECK(m[std::size_t(2*8+2)]==0);
    CHECK(m[std::size_t(0)]==0);
    CHECK(masks.value().unassigned[std::size_t(0)]==255);
    CHECK(masks.value().unassigned[std::size_t(2*8+2)]==255);
}
TEST("M03 malformed low resolution mask is rejected"){
    ProposalSet p;
    p.sourceSize={4,4};
    p.candidates.push_back({Id::fromNumber(1),{0,0,4,4},Bytes(12,255),0});
    auto result=projectMasks(p,{{4,4},true});
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::CorruptData);
}
