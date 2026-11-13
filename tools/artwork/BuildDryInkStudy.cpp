// Real renderer comparison: same geometry/pressure/style, only engine/tool differ.
#include "render/CoverageRasterizer.h"
#include <fstream>
#include <iostream>
#include <cassert>
#include <cmath>
using namespace drawborad;
int main(int argc,char**argv){assert(argc==2);StrokeEvent e;e.engineVersion=12;e.toolId="dry-ink";e.seed=431;e.style.width=48;e.style.hardness=1;e.endUs=1000;
for(unsigned i=0;i<=100;i++){double t=i/100.;e.geometry.push_back({30+440*t,70+45*std::sin(t*4),.12+.85*std::sin(t*3.141592653589793),{},{},i*10,0,0});}
std::vector<unsigned char> rgb(1024*512*3,245);
for(unsigned side=0;side<2;side++)for(unsigned row=0;row<3;row++){auto line=e;line.engineVersion=side?12:2;line.toolId=side?"dry-ink":"marker";line.style.width=row==0?48:(row==1?18:5);for(auto& p:line.geometry)p.y+=row*150;auto cov=strokeCoverage(line,{0,0,512,512},1000);assert(cov);for(unsigned y=0;y<512;y++)for(unsigned x=0;x<512;x++){unsigned char value=static_cast<unsigned char>(245-(std::uint32_t(cov.value()[y*512+x])*220+32767)/65535);for(unsigned c=0;c<3;c++){auto& dst=rgb[(y*1024+x+side*512)*3+c];dst=std::min(dst,value);}}}
std::ofstream out(argv[1],std::ios::binary);out<<"P6\n1024 512\n255\n";out.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());assert(out.good());std::cout<<"PASS normal marker left / dry ink right, same 3 paths widths 48/18/5\n";}
