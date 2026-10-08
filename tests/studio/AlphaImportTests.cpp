#include "Test.h"
#include "apps/studio/AlphaImportPolicy.h"
#include <array>
#include <utility>
using namespace drawborad;
namespace {
AlphaReferenceCandidate sample(){return {true,true,3*1024*1024,1024,768};}
}
TEST("Alpha preview accepts a decoded local image under the limits"){
    CHECK(validateAlphaReferenceCandidate(sample()));
}
TEST("Alpha preview rejects nonlocal URLs"){
    auto c=sample(); c.localFile=false;
    auto r=validateAlphaReferenceCandidate(c);
    CHECK(!r);
    CHECK(r.error().code==ErrorCode::InvalidInput);
    CHECK(r.error().messageKey=="alpha.reference-local");
}
TEST("Alpha preview rejects untrusted decoding failures"){
    auto c=sample(); c.decoded=false;
    auto r=validateAlphaReferenceCandidate(c);
    CHECK(!r);
    CHECK(r.error().messageKey=="alpha.reference-decode");
}
TEST("Alpha preview enforces encoded size limits"){
    for(auto size:{0ull, 32ull*1024ull*1024ull+1ull}){
        auto c=sample(); c.encodedBytes=size;
        auto r=validateAlphaReferenceCandidate(c);
        CHECK(!r);
        CHECK(r.error().messageKey=="alpha.reference-file-size");
    }
}
TEST("Alpha preview bounds dimensions before allocating canvas"){
    for(const auto& dims:std::array<std::pair<unsigned,unsigned>,4>{{{0u,768u},{4097u,768u},{100u,0u},{4096u,4097u}}}){
        auto c=sample();c.width=dims.first;c.height=dims.second;
        auto r=validateAlphaReferenceCandidate(c);
        CHECK(!r);
        CHECK(r.error().messageKey=="alpha.reference-dimensions");
    }
    auto c=sample();c.width=4096;c.height=4096;
    CHECK(validateAlphaReferenceCandidate(c));
}
