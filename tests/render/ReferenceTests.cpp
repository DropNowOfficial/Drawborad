#include "Test.h"
#include "RenderFixture.h"
#include "render/ReferenceRenderer.h"
using namespace drawborad;
TEST("R01 deterministic tile replay and local indexing"){
 auto s=fixture::withEvents(fixture::snapshot(),{fixture::stroke(fixture::snapshot(),500,40,530,40),fixture::stroke(fixture::snapshot(),800,800,820,820,11)});
 ReferenceRenderer r; EventLimit l{s.revision,s.eventIndex->size(),UINT64_MAX};auto a=r.render(s,{Id::fromNumber(3),0,0,0},l);auto b=r.render(s,{Id::fromNumber(3),0,0,0},l);CHECK(a&&b);CHECK(canonicalPixelHash(a.value())==canonicalPixelHash(b.value()));CHECK(r.statistics().sourceAssetReads==0);CHECK(r.statistics().rasterizedEvents<4);
}
TEST("R01 CMYKA16 stays native and eraser affects only selected layer"){
 auto base=fixture::snapshot(ColorMode::CMYK,Depth::U16,512,512);auto e=fixture::stroke(base,100,100,160,100);auto er=e;er.id=Id::fromNumber(12);er.toolId="eraser";er.startUs=201;er.endUs=300;for(auto& p:er.geometry)p.timestampUs+=201;auto s=fixture::withEvents(base,{e,er});ReferenceRenderer r;auto t=r.render(s,{Id::fromNumber(3),0,0,0},{s.revision,2,UINT64_MAX});CHECK(t);CHECK(t.value().format.mode==ColorMode::CMYK);CHECK(t.value().format.depth==Depth::U16);CHECK(t.value().format.channelCount()==5);
}
