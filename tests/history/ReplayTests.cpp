#include "Test.h"
#include "RenderFixture.h"
#include "history/ReplayEngine.h"
using namespace drawborad;
TEST("R04 blank reverse forward and direct seek agree") {auto base=fixture::snapshot(ColorMode::RGB,Depth::U8,512,512);auto s=fixture::withEvents(base,{fixture::stroke(base,20,20,80,20),fixture::stroke(base,100,100,160,100,11)});ReplayEngine r;ReplayRequest q{s,{Id::fromNumber(3),0,0,0},0,UINT64_MAX};auto blank=r.seek(q);CHECK(blank);q.eventCount=2;auto direct=r.seek(q);CHECK(direct);auto back=r.step(EventDirection::Backward);CHECK(back);auto forward=r.step(EventDirection::Forward);CHECK(forward);CHECK(forward.value().pixelHash==direct.value().pixelHash);CHECK(r.statistics().sourceAssetReads==0);}
TEST("R04 index partitions long history into bounded chunks") {EventList e;auto s=fixture::snapshot();for(int i=0;i<600;i++){auto x=fixture::stroke(s,i%400,10,i%400+1,10,100+i);x.startUs=i*1000;x.endUs=x.startUs+500;for(auto& p:x.geometry)p.timestampUs=x.startUs++;e.push_back(x);}EventIndex idx;CHECK(idx.build(e));CHECK(idx.chunks().size()==3);}
