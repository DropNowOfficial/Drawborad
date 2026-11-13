#include "brush/CubicInkStroke.h"
#include "brush/FreehandStroke.h"
#include "brush/BucketFill.h"
#include "render/ReferenceRenderer.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
// Isolated executable only: track allocations during refused commands, not setup.
// Prevent compiler inlining from diagnosing the deliberately paired malloc/free
// implementation as a mismatched new/delete. No production allocator is changed.
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE
#endif
static bool tracking=false;static std::size_t largest=0,total=0;
TEST_NOINLINE void* operator new(std::size_t n){if(tracking){largest=std::max(largest,n);total+=n;}if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void operator delete(void*p)noexcept{std::free(p);}
TEST_NOINLINE void operator delete(void*p,std::size_t)noexcept{std::free(p);}
using namespace drawborad;
void startTracking(){largest=total=0;tracking=true;}
void assertSmall(const char* name){tracking=false;
 // Generous fixed bounds for bookkeeping, far below copying the >60MiB history.
 // Deliberately do not assert allocator-specific exact byte counts.
 assert(largest<64*1024&&total<1024*1024);std::cout<<name<<" largest="<<largest<<" total="<<total<<" bytes\n";}
int main(){
 NewDocumentRequest request;request.dimensions={128,128,Unit::Px,300};auto spec=validateDocument(request,{});assert(spec);auto made=Document::create(spec.value(),Id::fromNumber(1));assert(made);auto&doc=made.value();const auto layer=Id::fromNumber(2);
 auto add=addFillLayerCommand(doc.snapshot(),layer);assert(add&&doc.apply(add.value(),doc.snapshot().revision));auto blank=doc.snapshot();
 std::array<std::array<double,2>,4> controls{{{20,20},{20,100},{100,100},{100,20}}};ColorValue black;
 auto create=cubicInkStrokeCommand(blank,layer,Id::fromNumber(10),controls,4,black);assert(create);auto newEvent=std::get<Paint>(create.value().operations.front()).event;
 auto legacy=blank;auto oldEvents=std::make_shared<EventList>();StrokeEvent old;old.id=Id::fromNumber(20);old.layerId=layer;old.objectId=blank.nodes->at(layer).objectId;old.parentRevision=blank.revision;old.engineVersion=1;old.toolId="pencil";old.geometry.assign(500000,{20,20,1,{},{},0,0,0});oldEvents->push_back(old);old.id=Id::fromNumber(21);oldEvents->push_back(std::move(old));legacy.eventIndex=oldEvents;assert(validateSnapshot(legacy));auto oldDoc=Document::fromSnapshot(legacy);assert(oldDoc);
 startTracking();auto helperDenied=cubicInkStrokeCommand(legacy,layer,Id::fromNumber(11),controls,4,black);assertSmall("legacy-over-budget helper");assert(!helperDenied&&helperDenied.error().messageKey=="cubic.raw-point-budget");
 Command raw(Paint{newEvent,{0,0,128,128}});startTracking();auto rawDenied=oldDoc.value().apply(raw,legacy.revision);assertSmall("legacy-over-budget raw Paint");assert(!rawDenied&&rawDenied.error().messageKey=="cubic.raw-point-budget"&&oldDoc.value().snapshot().sameContent(legacy));
 auto oversized=newEvent;oversized.geometry.resize(100000,oversized.geometry.front());Command malformed(Paint{std::move(oversized),{0,0,128,128}});startTracking();auto invalid=doc.apply(malformed,blank.revision);assertSmall("noncanonical incoming raw Paint");assert(!invalid&&doc.snapshot().sameContent(blank));
 assert(doc.apply(create.value(),blank.revision));auto snapshot=doc.snapshot();auto mutableEvents=std::make_shared<EventList>(*snapshot.eventIndex);snapshot.eventIndex=mutableEvents;
 ReferenceRenderer renderer;auto initial=renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision});assert(initial&&renderer.statistics().cubicPreparations==1);
 mutableEvents->front().geometry[1].x=95;auto changed=renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision});assert(changed&&changed.value().bytes!=initial.value().bytes&&renderer.statistics().cubicPreparations==2);
 auto again=renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision});assert(again&&again.value().bytes==changed.value().bytes&&renderer.statistics().cubicPreparations==2);
 mutableEvents->front().color.channels[0]=1;auto colored=renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision});assert(colored&&colored.value().bytes!=changed.value().bytes&&renderer.statistics().cubicPreparations==3);
 mutableEvents->front().style.width=129;assert(!renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision}));mutableEvents->front().style.width=4;auto restored=renderer.renderRegion(snapshot,layer,{0,0,128,128},{snapshot.revision});assert(restored&&restored.value().bytes==colored.value().bytes&&renderer.statistics().cubicPreparations==3);
 std::cout<<"PASS actual native renderer detects existing23 alias handle/color edits, reuses stable cache, rejects invalid mutation and restores prior valid cache\n";
}
