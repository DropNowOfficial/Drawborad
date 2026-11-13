#include "brush/FloodFill.h"
#include <cassert>
#include <iostream>
#include <random>
#include <queue>
using namespace drawborad;
void boundaries() {
    std::vector<Rgba8> a(9,{255,255,255,255});
    a[1]=a[3]=a[5]=a[7]={0,0,0,255};
    auto isolated=floodFill(a,3,3,1,1,0);
    assert(isolated && isolated.value().pixelCount==1);
    assert(!floodFill(a,0,3,0,0,0));
    assert(!floodFill(a,3,3,3,0,0));
    assert(!floodFill(a,3,4,0,0,0));
    assert(floodFill(a,3,3,0,0,0,{8,20}).error().code==ErrorCode::BudgetExceeded);
    std::vector<Rgba8> solid(12,{1,2,3,255});
    assert(floodFill(solid,4,3,0,0,0,{12,2}).error().code==ErrorCode::BudgetExceeded);
    std::stop_source stop;
    stop.request_stop();
    assert(floodFill(a,3,3,0,0,0,{},stop.get_token()).error().code==ErrorCode::Cancelled);
    std::vector<Rgba8> gradient{{0,0,0,255},{10,10,10,255},{20,20,20,255}};
    assert(floodFill(gradient,3,1,0,0,10).value().pixelCount==2);
    std::vector<Rgba8> alpha{{0,0,0,0},{200,100,50,0},{200,100,50,255}};
    assert(floodFill(alpha,3,1,0,0,0).value().pixelCount==2);
}
void oracle() {
    std::mt19937 random(7);
    for (int trial=0;trial<500;++trial) {
        const unsigned w=1+random()%30,h=1+random()%30,s=random()%(w*h);
        std::vector<Rgba8> image(w*h);
        for (auto& p:image) p={std::uint8_t(random()%3),0,0,255};
        std::vector<bool> expected(w*h),actual(w*h);
        std::queue<unsigned> q; q.push(s); expected[s]=true;
        while (!q.empty()) {
            auto i=q.front();q.pop();
            auto visit=[&](unsigned j) {
                if (!expected[j] && image[j]==image[s]) {expected[j]=true;q.push(j);}
            };
            if (i%w) visit(i-1);
            if (i%w+1<w) visit(i+1);
            if (i>=w) visit(i-w);
            if (i+w<w*h) visit(i+w);
        }
        auto fill=floodFill(image,w,h,s%w,s/w,0);
        assert(fill);
        std::uint64_t n=0;
        for (auto run:fill.value().runs) {
            assert(run.length && run.y<h && run.x+run.length<=w);
            for (auto x=run.x;x<run.x+run.length;++x) {
                assert(!actual[run.y*w+x]); actual[run.y*w+x]=true; ++n;
            }
        }
        assert(actual==expected && n==fill.value().pixelCount);
    }
}
int main() {
    std::vector<Rgba8> image(25, {255,255,255,255});
    for (unsigned y=0;y<5;++y) image[y*5+2]={0,0,0,255};
    const auto original=image;
    auto fill=floodFill(image,5,5,0,0,0,{25,10});
    assert(fill && fill.value().pixelCount==10 && fill.value().runs.size()==5);
    for (const auto& run:fill.value().runs) assert(run.x==0 && run.length==2);
    assert(image==original);
    boundaries();
    oracle();
    std::vector<Rgba8> printCanvas(3300u*5100u,{255,255,255,255});
    auto printFill=floodFill(printCanvas,3300,5100,3299,5099,0);
    assert(printFill && printFill.value().pixelCount==16830000 &&
           printFill.value().runs.size()==5100);
    std::cout << "flood-fill tests passed\n";
}
