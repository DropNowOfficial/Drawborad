#include "Test.h"
#include "RenderFixture.h"
#include "render/GraphicsResources.h"
using namespace drawborad;
TEST("R06 pressure evicts unpinned cache first") {TileCache c(500);auto t=std::make_shared<TileData>();t->bytes.resize(200);ContentTileKey a{{Id::fromNumber(3),0,0,0},1},b{{Id::fromNumber(3),1,0,0},1};c.put(a,t);c.pin(a);c.put(b,t);DisplayCache d;ResourceBudget budget;MemoryPressure p{19*GiB,1*GiB,5*GiB,8*GiB,2*GiB};auto r=trimForPressure(c,d,p,budget);CHECK(r.tileBytesFreed>=200);CHECK(c.get(a)!=nullptr);CHECK(r.cpuFallback);}
TEST("R06 device loss clears disposable display state without changing document") {auto s=fixture::snapshot();auto before=s;DisplayCache d;GraphicsResources g;auto t=std::make_shared<TileData>();DisplayTileKey k{{Id::fromNumber(3),0,0,0},{},0,0,0,1};d.put(k,{t,0});auto r=recoverGraphicsDevice(g,d,true,s);CHECK(r);CHECK(g.generation==2);CHECK(d.size()==0);CHECK(s.sameContent(before));}
