#include "brush/CubicInkPreparation.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE
#endif
// Test-local allocation callback requests cancellation AFTER production entry.
static std::stop_source* stopper=nullptr;static std::size_t allocations=0;
TEST_NOINLINE void* operator new(std::size_t n){if(stopper&&++allocations==3)stopper->request_stop();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void operator delete(void*p)noexcept{std::free(p);}
TEST_NOINLINE void operator delete(void*p,std::size_t)noexcept{std::free(p);}
using namespace drawborad;
int main(){
 StrokeEvent e;e.engineVersion=24;e.toolId="cubic-path-closed-aa";e.style.width=3;e.geometry={{32,8,1,{},{},0,0,0},{50,14,1,{},{},0,0,0},{56,48,1,{},{},0,0,0},{30,54,1,{},{},0,0,0},{4,48,1,{},{},0,0,0},{26,10,1,{},{},0,0,0}};const auto unchanged=e;
 std::stop_source stop;assert(!stop.stop_requested());allocations=0;stopper=&stop;auto cancelled=prepareCubicPathGeometry(e,stop.get_token());stopper=nullptr;assert(allocations>=3&&stop.stop_requested()&&!cancelled&&cancelled.error().code==ErrorCode::Cancelled&&e==unchanged);
 auto events=std::make_shared<EventList>(1,e);CubicInkPreparation cache;auto first=cache.prepare(events);assert(first);const auto saved=cache.current();events->front().geometry[1].x=40;
 std::stop_source stop2;allocations=0;stopper=&stop2;auto rejected=cache.prepare(events,stop2.get_token());stopper=nullptr;assert(allocations>=3&&stop2.stop_requested()&&!rejected&&rejected.error().code==ErrorCode::Cancelled&&cache.current()==saved);auto recovered=cache.prepare(events);assert(recovered&&cache.current()!=saved&&cache.current()->authoredEvents.at(0)==events->front());
 std::cout<<"PASS allocation-triggered mid-call production24 cancellation, input immutable, prior cache retained and fresh retry succeeds; no exact visit-count claim.\n";
}
