#include "brush/PressureTaper.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
InputSample p(double x,double y=0){InputSample r;r.x=x;r.y=y;r.pressure=1;return r;}
int main(){
 auto half=taperMousePressure(std::vector<InputSample>{p(0),p(10)},.5);assert(half&&half.value().size()==3);assert(half.value()[1].x==5&&half.value()[1].pressure==1);
 auto shortLine=taperMousePressure(std::vector<InputSample>{p(0),p(.001)},.2);assert(shortLine&&shortLine.value().size()==4);assert(shortLine.value()[1].pressure==1);
 auto corner=taperMousePressure(std::vector<InputSample>{p(0),p(5),p(5,5)},.2);assert(corner);assert(std::any_of(corner.value().begin(),corner.value().end(),[](const auto& q){return q.x==5&&q.y==0;}));
 auto reversal=taperMousePressure(std::vector<InputSample>{p(0),p(5),p(0)},.2);assert(reversal);assert(std::any_of(reversal.value().begin(),reversal.value().end(),[](const auto& q){return q.x==5;}));
 std::vector<InputSample> many;for(unsigned i=0;i<1024;i++)many.push_back(p(i,double(i%2)));
 auto bounded=taperMousePressure(many,.23);assert(bounded&&bounded.value().size()<=1026&&bounded.value().size()>=1024);
 assert(!taperMousePressure(many,.50001));many.push_back(p(1024));assert(!taperMousePressure(many,.2));
 auto same=taperMousePressure(std::vector<InputSample>{p(2),p(2)},.2);assert(same&&same.value().size()==2&&same.value()[0].pressure==1);
 std::cout<<"PASS taper half/short/corner/reversal/1024-point and dot boundaries\n";
}
