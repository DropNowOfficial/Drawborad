#include "render/FineLineCoverage.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
using namespace drawborad;
StrokeEvent line(double width,double y){StrokeEvent e;e.engineVersion=10;e.toolId="fineliner-aa";e.style.width=width;e.style.hardness=1;e.geometry={{2,y,1,{},{},0,0,0},{30,y,1,{},{},100,0,0}};e.endUs=100;return e;}
int main(){
 auto e=line(.25,8.5);auto c=strokeCoverage(e,{0,0,32,16},100);assert(c);
 assert(*std::max_element(c.value().begin(),c.value().end())==16384);
 for(unsigned x=4;x<28;x++){unsigned sum=0;for(unsigned y=0;y<16;y++)sum+=c.value()[y*32+x];assert(sum==16384);}
 auto shifted=line(.25,8.25);auto d=strokeCoverage(shifted,{0,0,32,16},100);assert(d);
 for(unsigned x=4;x<28;x++){unsigned sum=0;for(unsigned y=0;y<16;y++)sum+=d.value()[y*32+x];assert(sum==16384);}
 auto thin=line(.05,8.5);auto thinC=strokeCoverage(thin,{0,0,32,16},100);assert(thinC);assert(*std::max_element(thinC.value().begin(),thinC.value().end())==3277);
 auto legacy=e;legacy.engineVersion=2;legacy.toolId="fineliner";auto old=strokeCoverage(legacy,{0,0,32,16},100);assert(old);assert(*std::max_element(old.value().begin(),old.value().end())==65535); // Old replay deliberately unchanged.
 auto split=e;split.geometry.insert(split.geometry.begin()+1,{16,8.5,1,{},{},50,0,0});auto parts=strokeCoverage(split,{0,0,32,16},100);assert(parts&&parts.value()==c.value());
 auto diagonal=line(.5,1.5);diagonal.geometry[0].x=1.5;diagonal.geometry[1].x=30.5;diagonal.geometry[1].y=30.5;
 auto diag=strokeCoverage(diagonal,{0,0,32,32},100);assert(diag);for(unsigned i=3;i<29;i++)assert(diag.value()[i*32+i]>0&&diag.value()[i*32+i]<=32768);
 auto edge=line(.5,8.5);edge.geometry[0].x=490;edge.geometry[1].x=530;
 auto left=strokeCoverage(edge,{480,0,32,16},100),right=strokeCoverage(edge,{512,0,32,16},100),whole=strokeCoverage(edge,{480,0,64,16},100);assert(left&&right&&whole);
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<64;x++)assert(whole.value()[y*64+x]==(x<32?left.value()[y*32+x]:right.value()[y*32+x-32]));
 auto partial=strokeCoverage(e,{0,0,32,16},50);assert(partial);assert(partial.value()[8*32+10]>0&&partial.value()[8*32+25]==0);
 auto zero=e;for(auto& p:zero.geometry)p.pressure=0;auto empty=strokeCoverage(zero,{0,0,32,16},100);assert(empty&&std::all_of(empty.value().begin(),empty.value().end(),[](auto v){return v==0;}));
 auto broad=line(32,20.25);broad.engineVersion=19;broad.toolId="smooth-ink-aa";
 auto broadPixels=strokeCoverage(broad,{0,0,64,64},100);assert(broadPixels);
 assert(std::any_of(broadPixels.value().begin(),broadPixels.value().end(),[](auto v){return v>0&&v<65535;}));
 auto mismatched=broad;mismatched.toolId="fineliner-aa";assert(!strokeCoverage(mismatched,{0,0,64,64},100));
 mismatched=broad;mismatched.style.width=129;assert(!strokeCoverage(mismatched,{0,0,64,64},100));
 auto broadEdge=broad;broadEdge.style.width=128;broadEdge.geometry[0].x=490;broadEdge.geometry[1].x=530;
 auto bl=strokeCoverage(broadEdge,{480,0,32,64},100),br=strokeCoverage(broadEdge,{512,0,32,64},100),bw=strokeCoverage(broadEdge,{480,0,64,64},100);assert(bl&&br&&bw);
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++)assert(bw.value()[y*64+x]==(x<32?bl.value()[y*32+x]:br.value()[y*32+x-32]));
 auto dot=broad;dot.geometry.resize(1);dot.startUs=dot.endUs=0;dot.geometry[0].timestampUs=0;dot.geometry[0].x=0;dot.geometry[0].y=0;dot.style.width=128;auto dotPixels=strokeCoverage(dot,{0,0,64,64},0);assert(dotPixels&&dotPixels.value()[0]>0);
 auto broadZero=broad;for(auto& p:broadZero.geometry)p.pressure=0;auto bz=strokeCoverage(broadZero,{0,0,64,64},100);assert(bz&&std::all_of(bz.value().begin(),bz.value().end(),[](auto v){return v==0;}));
 auto invalid=e;invalid.engineVersion=2;assert(!strokeCoverage(invalid,{0,0,32,16},100));
 invalid=e;invalid.style.width=.01;assert(!strokeCoverage(invalid,{0,0,32,16},100));
 invalid=e;invalid.geometry[1].timestampUs=0;assert(!strokeCoverage(invalid,{0,0,32,16},100));
 invalid=e;invalid.style.angle=1;assert(!strokeCoverage(invalid,{0,0,32,16},100));
 std::stop_source stop;stop.request_stop();assert(!strokeCoverage(e,{0,0,32,16},100,stop.get_token()));
 std::cout<<"PASS opt-in subpixel fine line, fractional shift, diagonal continuity, split invariance, tile seam, replay, pressure zero, unchanged legacy\n";
}
