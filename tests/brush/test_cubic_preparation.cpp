#include "brush/CubicInkPreparation.h"
#include "core/CubicInkEvent.h"
#include "render/CoverageRasterizer.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
StrokeEvent cubic(){StrokeEvent e;e.engineVersion=23;e.toolId="cubic-ink-aa";e.style.width=4;e.style.hardness=1;e.geometry={{20,20,1,{},{},0,0,0},{20,180,1,{},{},0,0,0},{180,180,1,{},{},0,0,0},{180,20,1,{},{},0,0,0}};return e;}
void mutableAliasRegression(){
 CubicInkPreparation cache;
 auto input=std::make_shared<EventList>(1,cubic());
 auto first=cache.prepare(input);assert(first);
 const auto oldControls=first.value()->authoredEvents.at(0);
 const auto oldGeometry=first.value()->renderEvents.at(0).geometry;
 input->front().geometry[1].x=130;
 auto edited=cache.prepare(input);
 assert(edited&&edited.value()!=first.value()&&edited.value()->renderEvents.at(0).geometry!=oldGeometry);
 assert(first.value()->authoredEvents.at(0)==oldControls&&first.value()->renderEvents.at(0).geometry==oldGeometry);
 auto previous=edited.value();
 input->front().style.width=8;
 auto width=cache.prepare(input);assert(width&&width.value()!=previous&&width.value()->renderEvents.at(0).style.width==8);
 previous=width.value();input->front().color.channels[0]=.8;
 auto color=cache.prepare(input);assert(color&&color.value()!=previous&&color.value()->renderEvents.at(0).color.channels[0]==.8);
 input->push_back(cubic());
 auto two=cache.prepare(input);assert(two&&two.value()->renderEvents.size()==2&&two.value()->sourceEventCount==2);
 input->front().engineVersion=19;input->front().toolId="smooth-ink-aa";
 auto one=cache.prepare(input);assert(one&&one.value()->renderEvents.size()==1&&one.value()->renderEvents.contains(1));
 previous=one.value();input->front().geometry[0].x=7;
 auto legacyOnly=cache.prepare(input);assert(legacyOnly&&legacyOnly.value()==previous);
 input->at(1).engineVersion=19;
 auto zero=cache.prepare(input);assert(zero&&zero.value()->renderEvents.empty()&&zero.value()->authoredEvents.empty());
 input->front()=cubic();
 auto restored=cache.prepare(input);assert(restored&&restored.value()->renderEvents.size()==1&&restored.value()->renderEvents.contains(0));
 input->pop_back();
 auto smaller=cache.prepare(input);assert(smaller&&smaller.value()!=restored.value()&&smaller.value()->sourceEventCount==1);
 previous=smaller.value();input->front().style.grain.resize(8*1024*1024);
 auto rejected=cache.prepare(input);assert(!rejected&&rejected.error().messageKey=="freehand.style"&&cache.current()==previous);
 // The independent allocation-injection probe verifies rejection does not copy this grain.
 input->front()=cubic();assert(cache.prepare(input));previous=cache.current();
 input->resize(8193);assert(!cache.prepare(input)&&cache.current()==previous);
}
void sharedCoreRegression(){
 auto e=cubic();assert(validateCubicInkEvent(e));auto g=prepareCubicInkGeometry(e);assert(g&&g.value().samples.size()>4&&g.value().visits<=8191);
 auto point=e;for(auto& p:point.geometry){p.x=5;p.y=7;}auto dot=prepareCubicInkGeometry(point);assert(dot&&dot.value().samples.size()==2&&dot.value().visits==1);
 auto reverse=e;reverse.geometry[0].x=0;reverse.geometry[1].x=300;reverse.geometry[2].x=-300;reverse.geometry[3].x=1;for(auto& p:reverse.geometry)p.y=0;
 auto reversal=prepareCubicInkGeometry(reverse);assert(reversal&&reversal.value().samples.size()>4);bool past=false;for(auto p:reversal.value().samples)past|=p.x<0||p.x>1;assert(past);
 for(int mode=0;mode<7;++mode){auto invalid=e;if(mode==0)invalid.style.grain.resize(4096);if(mode==1)invalid.geometry[1].pressure=.5;if(mode==2)invalid.geometry[2].tiltX=0;if(mode==3)invalid.geometry[1].timestampUs=1;if(mode==4)invalid.geometry.pop_back();if(mode==5)invalid.style.width=.49;if(mode==6)invalid.geometry[0].x=NAN;assert(!validateCubicInkEvent(invalid)&&!prepareCubicInkGeometry(invalid));}
 EventList legacy(8193);assert(validateCubicDocumentBudget(legacy));CubicInkPreparation legacyCache;assert(legacyCache.prepare(std::make_shared<EventList>(legacy)));
 legacy.back()=e;auto eventLimit=validateCubicDocumentBudget(legacy);assert(!eventLimit&&eventLimit.error().messageKey=="cubic.event-budget");
 auto source=std::make_shared<EventList>(2);source->front()=e;source->back().geometry.resize(999996);assert(validateCubicDocumentBudget(*source));
 CubicInkPreparation cache;auto first=cache.prepare(source);assert(first);source->back().geometry.push_back({});
 auto rawLimit=validateCubicDocumentBudget(*source);assert(!rawLimit&&rawLimit.error().messageKey=="cubic.raw-point-budget");assert(!cache.prepare(source)&&cache.current()==first.value());
 source->front().engineVersion=19;assert(validateCubicDocumentBudget(*source)&&cache.prepare(source));
 EventList curves(257,e);auto curveLimit=validateCubicDocumentBudget(curves);assert(!curveLimit&&curveLimit.error().messageKey=="cubic.curve-budget");
 EventList many(200,e);for(auto& c:many){c.geometry[1].y=5100;c.geometry[2].x=3300;c.geometry[2].y=5100;c.geometry[3].x=3300;}
 auto expanded=validateCubicDocumentBudget(many);assert(!expanded&&expanded.error().messageKey=="cubic.expanded-budget");
 std::stop_source stopped;stopped.request_stop();assert(!prepareCubicInkGeometry(e,stopped.get_token())&&!validateCubicDocumentBudget({},stopped.get_token()));
}
int main(){
 CubicInkPreparation cache;auto input=std::make_shared<EventList>();input->push_back(cubic());const auto unchanged=*input;
 auto prepared=cache.prepare(input);assert(prepared&&*input==unchanged);auto stable=prepared.value();assert(stable->source==input&&stable->renderEvents.size()==1&&stable->expandedPoints>4);
 const auto&render=stable->renderEvents.at(0);assert(render.engineVersion==19&&render.geometry.size()<=4096&&render.geometry.front()==input->front().geometry.front()&&render.geometry.back()==input->front().geometry.back());
 auto same=cache.prepare(input);assert(same&&same.value()==stable);
 auto pixels=strokeCoverage(render,{0,0,256,256},UINT64_MAX);assert(pixels);auto left=strokeCoverage(render,{0,0,128,256},UINT64_MAX),right=strokeCoverage(render,{128,0,128,256},UINT64_MAX);assert(left&&right);for(size_t y=0;y<256;++y)for(size_t x=0;x<256;++x)assert(pixels.value()[y*256+x]==(x<128?left.value()[y*128+x]:right.value()[y*128+x-128]));
 // Production23 coverage equivalence will be asserted when renderer admission is connected.
 // Authored controls remain23; the preparation surrogate alone is19.
 auto native23=strokeCoverage(input->front(),{0,0,256,256},UINT64_MAX);assert(native23&&native23.value()==pixels.value());
 auto edited=std::make_shared<EventList>(*input);edited->front().geometry[1].x=120;auto changed=cache.prepare(edited);assert(changed&&changed.value()!=stable&&stable->source==input&&*input==unchanged);
 auto changedPixels=strokeCoverage(changed.value()->renderEvents.at(0),{0,0,256,256},UINT64_MAX);assert(changedPixels&&changedPixels.value()!=pixels.value());
 auto previous=cache.current();std::stop_source stop;stop.request_stop();assert(!cache.prepare(input,stop.get_token())&&cache.current()==previous);
 for(int invalid=0;invalid<5;++invalid){auto bad=std::make_shared<EventList>(*input);auto&e=bad->front();if(invalid==0)e.geometry[1].pressure=.5;if(invalid==1)e.geometry[2].tiltX=10;if(invalid==2)e.geometry[1].timestampUs=1;if(invalid==3)e.geometry.pop_back();if(invalid==4)e.style.width=129;assert(!cache.prepare(bad)&&cache.current()==previous);}
 auto hostile=std::make_shared<EventList>(*input);hostile->front().geometry[1].x=1e9;hostile->front().geometry[2].x=-1e9;assert(!cache.prepare(hostile)&&cache.current()==previous);
 auto count=std::make_shared<EventList>(257,cubic());assert(!cache.prepare(count)&&cache.current()==previous);
 auto expanded=std::make_shared<EventList>(200,cubic());for(auto&e:*expanded){e.geometry[1].y=5100;e.geometry[2].x=3300;e.geometry[2].y=5100;e.geometry[3].x=3300;}auto denied=cache.prepare(expanded);assert(!denied&&denied.error().messageKey=="cubic.expanded-budget"&&cache.current()==previous);
 auto legacy=std::make_shared<EventList>();auto old=render;legacy->push_back(old);auto empty=cache.prepare(legacy);assert(empty&&empty.value()->renderEvents.empty()&&legacy->front()==old);
 mutableAliasRegression();sharedCoreRegression();
 std::cout<<"PASS staged cubic preparation: identity reuse and alias invalidation, native tiled coverage, edited handles, immutable originals, bounded aggregate geometry, atomic failure, legacy unchanged; NOT yet production format/UI\n";
}
