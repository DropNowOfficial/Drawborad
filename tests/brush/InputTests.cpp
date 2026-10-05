#include "Test.h"
#include "brush/BrushEngine.h"
using namespace drawborad;
TEST("R03 zero pressure and absent tilt stay truthful") {TabletPacket p;p.x=1;p.y=2;p.pressure=0.0;p.capabilities=PressureCap;auto s=normalizeTabletSample(p);CHECK(s.pressure==0);CHECK(!s.tiltX);TabletPacket m;m.x=1;m.y=2;m.capabilities=0;auto mouse=normalizeTabletSample(m);CHECK(mouse.pressure==1);CHECK(!mouse.tiltX);}
TEST("R03 same samples and seed compile identically") {BrushSettings b;b.objectId=Id::fromNumber(2);b.layerId=Id::fromNumber(3);b.parentRevision=1;b.seed=44;b.color.activeChannels=3;std::vector<InputSample> v{{0,0,0.2,{}, {},10,0,0},{20,0,1,{}, {},20,0,0}};BrushEngine e;auto a=e.compile(v,b);auto z=e.compile(v,b);CHECK(a&&z);CHECK(a.value()==z.value());auto er=e.compile(v,b,true);CHECK(er&&er.value().toolId=="eraser");}
