#include "Test.h"
#include "RenderFixture.h"
#include "view/Viewport.h"
#include <cmath>
using namespace drawborad;
TEST("R05 DPI values do not change document coordinates") {for(double d:{1.0,1.25,1.5,2.0,2.5}){Viewport v;CHECK(v.configure(3.25,d,17,-9));DocumentPoint p{123.4,55.6};auto q=v.toDocument(v.toScreen(p));CHECK(std::hypot(q.x-p.x,q.y-p.y)<0.01);} }
TEST("R05 zoom anchor remains fixed and locked node is not paintable") {Viewport v;CHECK(v.configure(2,1.5,10,20));ScreenPoint a{300,200};auto before=v.toDocument(a);CHECK(v.zoomAt(1.5,a));auto after=v.toDocument(a);CHECK(std::hypot(before.x-after.x,before.y-after.y)<1e-9);auto s=fixture::snapshot();auto ns=std::make_shared<NodeMap>(*s.nodes);ns->at(Id::fromNumber(2)).locked=true;s.nodes=ns;CHECK(!hitTest(s,{10,10}));}
