#include "Test.h"
#include "review/ReviewSession.h"
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
