#include "render/TileCache.h"
#include <chrono>
#include <algorithm>
#include <iostream>
using namespace drawborad;
int main(){TileCache c(64*1024*1024);TileData t;t.width=t.height=512;t.format={ColorMode::RGB,Depth::U8,true};t.rowStride=512*4;t.bytes.resize(t.rowStride*512);auto p=std::make_shared<const TileData>(t);for(int i=0;i<128;i++)c.put({{Id::fromNumber(2),i%16,i/16,0},std::uint64_t(i)},p);std::vector<double> ms;for(int i=0;i<1000;i++){auto a=std::chrono::steady_clock::now();(void)c.get({{Id::fromNumber(2),i%16,(i/16)%8,0},std::uint64_t(i%128)});auto b=std::chrono::steady_clock::now();ms.push_back(std::chrono::duration<double,std::milli>(b-a).count());}std::sort(ms.begin(),ms.end());std::cout<<"{\"scenario\":\"cache_lookup\",\"samples\":1000,\"p95_ms\":"<<ms[949]<<",\"measured\":true}\n";}
