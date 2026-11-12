#include "render/DryInkCoverage.h"
#include <cassert>
#include <algorithm>
#include <numeric>
#include <iostream>
using namespace drawborad;
StrokeEvent line(double pressure=1){StrokeEvent e;e.engineVersion=12;e.toolId="dry-ink";e.style.width=20;e.style.hardness=1;e.geometry={{4,32,pressure,{},{},0,0,0},{124,32,pressure,{},{},100,0,0}};e.endUs=100;return e;}
int main(){
 auto e=line();auto c=strokeCoverage(e,{0,0,128,64},100);assert(c);
 auto split=e;split.geometry.insert(split.geometry.begin()+1,{64,32,1,{},{},50,0,0});auto d=strokeCoverage(split,{0,0,128,64},100);assert(d&&d.value()==c.value());
 auto seeded=e;seeded.seed=31571;auto sc=strokeCoverage(seeded,{0,0,128,64},100);auto scAgain=strokeCoverage(seeded,{0,0,128,64},100);assert(sc&&scAgain&&sc.value()==scAgain.value()&&sc.value()!=c.value());
 auto low=line(.25);auto lc=strokeCoverage(low,{0,0,128,64},100);assert(lc);assert(std::accumulate(lc.value().begin(),lc.value().end(),0ull)<std::accumulate(c.value().begin(),c.value().end(),0ull)/2);
 auto zero=line(0);auto z=strokeCoverage(zero,{0,0,128,64},100);assert(z&&std::all_of(z.value().begin(),z.value().end(),[](auto v){return !v;}));
 // Longitudinal fibers: neighboring cross-stroke rows differ considerably,
 // while within-stroke variation over a few pixels is slow and continuous.
 unsigned cross=0,along=0;for(unsigned y=24;y<39;y++)for(unsigned x=20;x<110;x++){auto v=c.value()[y*128+x];cross+=std::abs(int(v)-int(c.value()[(y+1)*128+x]));along+=std::abs(int(v)-int(c.value()[y*128+x+1]));}assert(cross>along*4);
 auto vertical=e;vertical.geometry={{32,4,1,{},{},0,0,0},{32,124,1,{},{},100,0,0}};auto vc=strokeCoverage(vertical,{0,0,64,128},100);assert(vc); // Direction rotates with geometry, not paper coordinates.
 for(unsigned y=10;y<118;y++){unsigned sum=0;for(unsigned x=20;x<44;x++)sum+=vc.value()[y*64+x];assert(sum>0);}
 auto partial=strokeCoverage(e,{0,0,128,64},50);assert(partial);assert(partial.value()[32*128+30]>0&&partial.value()[32*128+100]==0);
 auto a=strokeCoverage(e,{0,0,64,64},100),b=strokeCoverage(e,{64,0,64,64},100);assert(a&&b);for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++)assert(c.value()[y*128+x]==(x<64?a.value()[y*64+x]:b.value()[y*64+x-64]));
 auto curved=e;curved.geometry.insert(curved.geometry.begin()+1,{64,44,.6,{},{},50,0,0});
 auto wholeCurve=strokeCoverage(curved,{0,0,128,64},100),leftCurve=strokeCoverage(curved,{0,0,64,64},100),rightCurve=strokeCoverage(curved,{64,0,64,64},100);assert(wholeCurve&&leftCurve&&rightCurve);
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++)assert(wholeCurve.value()[y*128+x]==(x<64?leftCurve.value()[y*64+x]:rightCurve.value()[y*64+x-64]));
 auto bad=e;bad.engineVersion=2;assert(!strokeCoverage(bad,{0,0,128,64},100));bad=e;bad.style.width=129;assert(!strokeCoverage(bad,{0,0,128,64},100));
 std::stop_source stop;stop.request_stop();assert(!strokeCoverage(e,{0,0,128,64},100,stop.get_token()));
 std::cout<<"PASS dry ink pressure, zero, directional fibers, split continuity, replay, tile seam, unsupported legacy, cancellation\n";
}
