#include "Test.h"
#include "codecs/psd/PsdReader.h"
#include "jobs/ImageQueue.h"
#include "review/ReviewSession.h"
#include <array>
#include <cstdint>
#include <vector>
using namespace drawborad;
namespace {
void put16(Bytes& b, std::uint16_t v){ b.push_back(std::uint8_t(v>>8)); b.push_back(std::uint8_t(v)); }
void put32(Bytes& b, std::uint32_t v){ b.push_back(std::uint8_t(v>>24)); b.push_back(std::uint8_t(v>>16)); b.push_back(std::uint8_t(v>>8)); b.push_back(std::uint8_t(v)); }
Bytes seedPsd(){ Bytes b{'8','B','P','S'}; put16(b,1); b.insert(b.end(),6,0); put16(b,4); put32(b,64); put32(b,64); put16(b,8); put16(b,3); put32(b,0); put32(b,0); put32(b,0); return b; }
std::uint64_t next(std::uint64_t& x){ x ^= x<<13; x ^= x>>7; x ^= x<<17; return x; }
}
TEST("X13 PSD parser survives deterministic corruption sweep") {
    const auto seed=seedPsd(); CodecLimits limits; std::uint64_t rng=0xD0A0B0A0D13ull;
    for(int n=0;n<2000;n++){
        auto b=seed;
        const auto edits=1+int(next(rng)%6);
        for(int k=0;k<edits;k++){
            if((next(rng)&7)==0 && b.size()>1) b.resize(std::size_t(1+next(rng)%b.size()));
            else { const auto at=std::size_t(next(rng)%b.size()); b[at]=std::uint8_t(next(rng)); }
        }
        auto r=readPhotoshopHeader(b,limits);
        if(r){ CHECK(r.value().size.width>0); CHECK(r.value().size.height>0); CHECK(r.value().size.width<=30000); CHECK(r.value().size.height<=30000); }
    }
}
TEST("X13 queue processes twenty items without concurrent heavy work") {
    ImageQueue q; std::array<std::filesystem::path,20> paths{};
    for(std::size_t i=0;i<paths.size();++i) paths[i]="image-"+std::to_string(i)+".png";
    auto id=q.enqueue(paths,{}); CHECK(id);
    for(std::size_t i=0;i<paths.size();++i){
        CHECK(q.startAnalysis(i)); CHECK(q.concurrentHeavyJobs()==1);
        CHECK(q.analysisComplete(i,true)); CHECK(q.concurrentHeavyJobs()==0);
        CHECK(q.acceptReview(i,Revision(i+1))); CHECK(q.concurrentHeavyJobs()==1);
        CHECK(q.finish(i,true)); CHECK(q.concurrentHeavyJobs()==0); CHECK(q.items()[i].state==QueueState::Done);
    }
}
TEST("X13 mask edit revision loop remains deterministic") {
    ProposalSet p; p.sourceSize={64,64}; p.candidates.push_back({Id::fromNumber(1),{0,0,64,64},{},0});
    MaskSet masks; masks.dimensions=p.sourceSize; masks.fullResolutionMasks.push_back(Bytes(4096,0)); masks.unassigned.assign(4096,255);
    auto s=ReviewSession::create(p,masks,1); CHECK(s);
    for(int i=0;i<250;i++){
        auto rev=s.value().revision(); Rect r{std::int64_t(i%56),std::int64_t((i*7)%56),8,8};
        CHECK(s.value().apply({i%2?MaskEditOp::SubtractBrush:MaskEditOp::AddBrush,0,r,1,rev}));
        if((i%17)==0){ CHECK(s.value().undo()); }
    }
    auto accepted=s.value().accept(s.value().revision()); CHECK(accepted); CHECK(accepted.value().canReconstruct);
}
