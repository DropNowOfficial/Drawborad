#include "Test.h"
#include "RenderFixture.h"
#include "render/ReferenceRenderer.h"
#include "view/LayerTree.h"
using namespace drawborad;
TEST("R01 deterministic tile replay and local indexing"){
 auto s=fixture::withEvents(fixture::snapshot(),{fixture::stroke(fixture::snapshot(),500,40,530,40),fixture::stroke(fixture::snapshot(),800,800,820,820,11)});
 ReferenceRenderer r; EventLimit l{s.revision,s.eventIndex->size(),UINT64_MAX};auto a=r.render(s,{Id::fromNumber(3),0,0,0},l);auto b=r.render(s,{Id::fromNumber(3),0,0,0},l);CHECK(a&&b);CHECK(canonicalPixelHash(a.value())==canonicalPixelHash(b.value()));CHECK(r.statistics().sourceAssetReads==0);CHECK(r.statistics().rasterizedEvents<4);
}
TEST("R01 CMYKA16 stays native and eraser affects only selected layer"){
 auto base=fixture::snapshot(ColorMode::CMYK,Depth::U16,512,512);auto e=fixture::stroke(base,100,100,160,100);auto er=e;er.id=Id::fromNumber(12);er.toolId="eraser";er.startUs=201;er.endUs=300;for(auto& p:er.geometry)p.timestampUs+=201;auto s=fixture::withEvents(base,{e,er});ReferenceRenderer r;auto t=r.render(s,{Id::fromNumber(3),0,0,0},{s.revision,2,UINT64_MAX});CHECK(t);CHECK(t.value().format.mode==ColorMode::CMYK);CHECK(t.value().format.depth==Depth::U16);CHECK(t.value().format.channelCount()==5);
}
TEST("R01 object masks clip display copies without altering raw replay checkpoints"){
 auto s=fixture::snapshot(ColorMode::RGB,Depth::U8,512,512);
 s=fixture::withEvents(s,{fixture::stroke(s,20,40,100,40)});
 auto nodes=std::make_shared<NodeMap>(*s.nodes);
 auto resources=std::make_shared<ResourceMap>(*s.resources);
 Bytes mask(512*512,128);mask[40*512+60]=0;
 const auto hash=sha256(mask);
 (*resources)[hash]={hash,ResourceKind::Mask,std::make_shared<const Bytes>(mask),{}};
 const auto layer=Id::fromNumber(3);const auto object=nodes->at(layer).parent;
 nodes->at(object).maskId=hash;s.nodes=nodes;s.resources=resources;
 CHECK(!nativeLayerPaintable(s,layer)); // Soft object groups need true group compositing.
 nodes->at(object).maskId={};nodes->at(layer).maskId=hash;
 Bytes binary(512*512,255);binary[40*512+60]=0;const auto binaryHash=sha256(binary);
 (*resources)[binaryHash]={binaryHash,ResourceKind::Mask,std::make_shared<const Bytes>(binary),{}};
 nodes->at(object).maskId=binaryHash;
 CHECK(nativeLayerPaintable(s,layer));
 ReferenceRenderer renderer;
 const EventLimit limit{s.revision,s.eventIndex->size(),UINT64_MAX};
 auto raw=renderer.renderRegion(s,layer,{50,40,11,1},limit);CHECK(raw);
 auto display=nativeMaskedTile(s,layer,raw.value(),50,40);CHECK(display);
 CHECK(display.value().bytes[3]==std::uint8_t((unsigned(raw.value().bytes[3])*128+127)/255));
 CHECK(display.value().bytes[10*4+3]==0);
 auto rawAgain=renderer.renderRegion(s,layer,{50,40,11,1},limit);CHECK(rawAgain);
 CHECK(rawAgain.value().bytes==raw.value().bytes);
 auto displayAgain=nativeMaskedTile(s,layer,rawAgain.value(),50,40);CHECK(displayAgain);
 CHECK(displayAgain.value().bytes==display.value().bytes);
 CHECK(renderer.statistics().sourceAssetReads==0);
 nodes->at(layer).transform.affine[4]=1;
 CHECK(!nativeLayerSupported(s,layer));
}
