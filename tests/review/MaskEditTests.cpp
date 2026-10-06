#include "Test.h"
#include "review/ReviewSession.h"
#include <algorithm>
using namespace drawborad;
TEST("M04 edit undo restores mask and stale accept is rejected"){ProposalSet p;p.sourceSize={8,8};p.candidates.push_back({Id::fromNumber(1),{0,0,8,8},{},0});MaskSet m;m.dimensions={8,8};m.fullResolutionMasks.push_back(Bytes(64,0));m.unassigned.assign(64,255);auto s=ReviewSession::create(p,m,3);CHECK(s);auto before=s.value().masks().fullResolutionMasks[0];CHECK(s.value().apply({MaskEditOp::AddBrush,0,{1,1,2,2},1,3}));CHECK(!s.value().accept(3));CHECK(s.value().undo());CHECK(s.value().masks().fullResolutionMasks[0]==before);}

TEST("M04 undo of lock restores editability"){
    ProposalSet p; p.sourceSize={4,4}; p.candidates.push_back({Id::fromNumber(1),{0,0,4,4},{},0});
    MaskSet m; m.dimensions={4,4}; m.fullResolutionMasks.push_back(Bytes(16,0)); m.unassigned.assign(16,255);
    auto s=ReviewSession::create(p,m,10); CHECK(s);
    CHECK(s.value().apply({MaskEditOp::Lock,0,{0,0,0,0},0,10}));
    CHECK(s.value().undo());
    auto rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::AddBrush,0,{0,0,1,1},1,rev}));
}

TEST("M04 merge split transform are reversible and isolated"){
    ProposalSet p; p.sourceSize={8,8};
    p.candidates.push_back({Id::fromNumber(1),{0,0,4,8},{},1});
    p.candidates.push_back({Id::fromNumber(2),{4,0,4,8},{},1});
    MaskSet m; m.dimensions={8,8};
    Bytes a(64,0), b(64,0);
    for(std::uint64_t y=0;y<8;y++){ a[std::size_t(y*8+1)]=255; b[std::size_t(y*8+6)]=255; }
    m.fullResolutionMasks={a,b}; m.unassigned.assign(64,255);
    auto s=ReviewSession::create(p,m,1); CHECK(s);

    MaskEdit merge; merge.operation=MaskEditOp::Merge; merge.objectIndex=0; merge.expectedRevision=1; merge.secondaryIndex=1;
    CHECK(s.value().apply(merge));
    CHECK(s.value().masks().fullResolutionMasks.size()==1);
    CHECK(s.value().masks().fullResolutionMasks[0][1]==255);
    CHECK(s.value().masks().fullResolutionMasks[0][6]==255);
    CHECK(s.value().undo());
    CHECK(s.value().masks().fullResolutionMasks.size()==2);
    CHECK(s.value().masks().fullResolutionMasks[0]==a);
    CHECK(s.value().masks().fullResolutionMasks[1]==b);

    auto rev=s.value().revision();
    MaskEdit split; split.operation=MaskEditOp::Split; split.objectIndex=0; split.region={0,0,4,8}; split.expectedRevision=rev;
    CHECK(s.value().apply(split));
    CHECK(s.value().masks().fullResolutionMasks.size()==3);
    CHECK(std::count(s.value().masks().fullResolutionMasks[2].begin(),s.value().masks().fullResolutionMasks[2].end(),255)>0);
    CHECK(s.value().undo());
    CHECK(s.value().masks().fullResolutionMasks.size()==2);

    rev=s.value().revision();
    MaskEdit transform; transform.operation=MaskEditOp::Transform; transform.objectIndex=0; transform.expectedRevision=rev;
    transform.transform.affine={1,0,0,1,2,0};
    CHECK(s.value().apply(transform));
    CHECK(s.value().masks().fullResolutionMasks[0][3]==255);
    CHECK(s.value().masks().fullResolutionMasks[0][1]==0);
    CHECK(s.value().undo());
    CHECK(s.value().masks().fullResolutionMasks[0]==a);
}

TEST("M04 prompt refine is explicit and does not mutate accepted masks"){
    ProposalSet p;
    p.sourceSize={8,8};
    p.candidates.push_back({Id::fromNumber(1),{0,0,8,8},Bytes(256*256,0),0});
    MaskSet m;
    m.dimensions={8,8};
    m.fullResolutionMasks.push_back(Bytes(64,0));
    m.unassigned.assign(64,255);
    bool called=false;
    auto session=ReviewSession::create(
        p,m,5,
        [&](const PromptSet& prompts,std::stop_token){
            called=true;
            CHECK(prompts.points.size()==1);
            ProposalSet refined=p;
            refined.candidates[0].score=.9;
            refined.candidates[0].lowResMask.assign(256*256,255);
            return Result<ProposalSet>{std::move(refined)};
        });
    CHECK(session);
    auto before=session.value().masks();
    PromptSet prompts;
    prompts.points.push_back({4,4,true});
    auto refined=session.value().refine(prompts);
    CHECK(refined);
    CHECK(called);
    CHECK(refined.value().candidates[0].score>.89);
    CHECK(session.value().masks().fullResolutionMasks==before.fullResolutionMasks);
    CHECK(session.value().revision()==5);
}


TEST("M04 subtract grow shrink feather lock and unlock obey revision contract") {
    ProposalSet p; p.sourceSize={7,7};
    p.candidates.push_back({Id::fromNumber(1),{0,0,7,7},{},1});
    MaskSet m; m.dimensions={7,7};
    Bytes mask(49,0); mask[3*7+3]=255;
    m.fullResolutionMasks.push_back(mask); m.unassigned.assign(49,255);
    auto s=ReviewSession::create(p,m,1); CHECK(s);

    auto rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::Grow,0,{0,0,0,0},1,rev}));
    CHECK(std::count(s.value().masks().fullResolutionMasks[0].begin(),s.value().masks().fullResolutionMasks[0].end(),255)>1);

    rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::Shrink,0,{0,0,0,0},1,rev}));
    CHECK(s.value().masks().fullResolutionMasks[0][3*7+3]==255);

    rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::Feather,0,{0,0,0,0},1,rev}));
    CHECK(std::any_of(s.value().masks().fullResolutionMasks[0].begin(),s.value().masks().fullResolutionMasks[0].end(),[](std::uint8_t v){return v>0&&v<255;}));

    rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::SubtractBrush,0,{3,3,1,1},1,rev}));
    CHECK(s.value().masks().fullResolutionMasks[0][3*7+3]==0);

    rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::Lock,0,{0,0,0,0},0,rev}));
    rev=s.value().revision();
    auto blocked=s.value().apply({MaskEditOp::AddBrush,0,{0,0,1,1},1,rev});
    CHECK(!blocked); CHECK(blocked.error().code==ErrorCode::Locked);
    CHECK(s.value().apply({MaskEditOp::Unlock,0,{0,0,0,0},0,rev}));
    rev=s.value().revision();
    CHECK(s.value().apply({MaskEditOp::AddBrush,0,{0,0,1,1},1,rev}));
}
