#include "Test.h"
#include "RenderFixture.h"
#include "render/DisplayCache.h"
using namespace drawborad;
TEST("R02 small edit invalidates only touched tile") {auto s=fixture::snapshot(ColorMode::RGB,Depth::U8,4096,4096);ChangeSet c;c.changedNodes={Id::fromNumber(3)};c.dirtyRegions={{513,513,10,10}};auto d=affectedTiles(c,s);CHECK(d.pigment.size()==1);CHECK(d.pigment.begin()->tileX==1);CHECK(d.pigment.begin()->tileY==1);}
TEST("R02 cache keeps pinned tile and profile change misses old display") {TileCache c(100);auto t=std::make_shared<TileData>();t->bytes.resize(70);ContentTileKey a{{Id::fromNumber(3),0,0,0},1},b{{Id::fromNumber(3),1,0,0},1};c.put(a,t);CHECK(c.pin(a));c.put(b,t);CHECK(c.get(a)!=nullptr);CHECK(c.get(b)==nullptr);DisplayCache d;Hash p;p.bytes[0]=1;DisplayTileKey k{{Id::fromNumber(3),0,0,0},p,0,0,0,1};d.put(k,{t,0});CHECK(d.get(k));d.invalidateProfile(p);CHECK(!d.get(k));}
