#include "platform/QtSolidDraftPreview.h"
#include "render/CoverageRasterizer.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace drawborad;
using Cache=QtSolidMaskCache;
static Cache::Shape shape(std::uint64_t id,double shift) {
    return {id,{QPointF(30+shift,30),QPointF(900,20),QPointF(900,850),
                QPointF(50+shift,900),QPointF(200,700),QPointF(10,350)}};
}
static QImage mask(const Cache::Shape& s) {
    QImage out(1024,1024,QImage::Format_Alpha8);out.fill(0);QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);QPainterPath p;p.setFillRule(Qt::OddEvenFill);
    const auto& c=s.controls;p.moveTo(c[0]);p.cubicTo(c[1],c[2],c[3]);p.cubicTo(c[4],c[5],c[0]);p.closeSubpath();
    painter.fillPath(p,Qt::black);assert(painter.end());return out;
}
static TileData background(unsigned w,unsigned h,Depth depth) {
    TileData t;t.width=w;t.height=h;t.format={ColorMode::RGB,depth,true};
    t.rowStride=w*4*(depth==Depth::U16?2:1)+8;t.bytes.resize(t.rowStride*h,213);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c) {
        const auto value=std::uint16_t(c==3?40000:10000+c*12000);
        const auto at=y*t.rowStride+x*4*(depth==Depth::U16?2:1)+c*(depth==Depth::U16?2:1);
        if(depth==Depth::U16){t.bytes[at]=std::uint8_t(value);t.bytes[at+1]=std::uint8_t(value>>8);}
        else t.bytes[at]=std::uint8_t((unsigned(value)+128)/257);
    }
    return t;
}
static TileData crop(const TileData& t,unsigned x,unsigned y,unsigned w,unsigned h) {
    auto out=background(w,h,t.format.depth);out.profileId=t.profileId;
    const auto bpp=t.format.depth==Depth::U16?8u:4u;
    for(unsigned row=0;row<h;++row)std::copy_n(t.bytes.begin()+(y+row)*t.rowStride+x*bpp,w*bpp,out.bytes.begin()+row*out.rowStride);
    return out;
}
static TileData reference(const TileData& bg,Rect region,std::span<const QtSolidDraftPaint> paints,
                          const std::vector<QImage>& images) {
    auto out=bg;std::vector<std::uint16_t> coverage(region.width*region.height);
    for(const auto& paint:paints) {
        const auto& image=images.at(paint.maskId-1);
        for(unsigned y=0;y<region.height;++y)for(unsigned x=0;x<region.width;++x)
            coverage[y*region.width+x]=std::uint16_t(image.constScanLine(int(region.y+y))[region.x+x])*257u;
        StrokeEvent e;e.toolId="paint";e.color=paint.color;e.style.opacity=paint.opacity;blendCoverage(out,e,coverage);
    }
    return out;
}
int main() {
    Cache cache;std::vector<Cache::Shape> shapes{shape(1,0),shape(2,100),shape(3,-20)};
    assert(cache.prepare(shapes,1024,1024,QString::fromLatin1(qVersion()))==Cache::Status::Ok);
    const auto generation=cache.generation(),rasters=cache.stats().rasterCalls;
    std::vector<QImage> images;for(const auto& s:shapes)images.push_back(mask(s));
    std::vector<QtSolidDraftPaint> paints{{1,{},.6},{2,{},.7},{3,{},.2}};
    paints[0].color.channels={1,.2,.1,0};paints[0].color.alpha=.5;
    paints[1].color.channels={.1,.3,1,0};paints[1].color.alpha=.8;
    paints[2].color.channels={.1,1,.3,0};
    for(auto depth:{Depth::U8,Depth::U16})for(const auto region:{Rect{128,128,512,512},Rect{257,500,511,203}}) {
        const auto bg=background(unsigned(region.width),unsigned(region.height),depth);const auto saved=bg;
        const auto result=renderQtSolidDraftPreview(cache,generation,region,paints,bg);
        assert(result && result.value()==reference(bg,region,paints,images) && bg==saved);
        // Arbitrary partitions cross both 256 mask chunk and 512 global tile boundaries.
        const auto bpp=depth==Depth::U16?8u:4u;
        for(unsigned y=0;y<region.height;y+=173)for(unsigned x=0;x<region.width;x+=191) {
            const auto w=unsigned(std::min<std::uint64_t>(191,region.width-x));
            const auto h=unsigned(std::min<std::uint64_t>(173,region.height-y));
            auto part=renderQtSolidDraftPreview(cache,generation,{region.x+x,region.y+y,w,h},paints,crop(bg,x,y,w,h));
            assert(part);
            for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w*bpp;++col)
                assert(part.value().bytes[row*part.value().rowStride+col]==result.value().bytes[(y+row)*result.value().rowStride+x*bpp+col]);
        }
        auto reverse=paints;std::reverse(reverse.begin(),reverse.end());
        const auto reversed=renderQtSolidDraftPreview(cache,generation,region,reverse,bg);
        assert(reversed && reversed.value()==reference(bg,region,reverse,images) && reversed.value()!=result.value());
        auto recolor=paints;recolor[0].color.channels={0,1,1,0};
        const auto colored=renderQtSolidDraftPreview(cache,generation,region,recolor,bg);
        assert(colored && colored.value()!=result.value());
        assert(cache.stats().rasterCalls==rasters);
    }
    auto bg=background(512,512,Depth::U8);const auto original=bg;const Rect region{0,0,512,512};
    const auto fails=[&](std::span<const QtSolidDraftPaint> ps,const TileData& input,Rect r=Rect{0,0,512,512}) {
        const auto saved=input;auto result=renderQtSolidDraftPreview(cache,generation,r,ps,input);
        assert(!result && input==saved && bg==original);
    };
    auto bad=paints;bad[2].maskId=999;fails(bad,bg); // late failure cannot publish earlier paint
    bad=paints;bad[1].maskId=bad[0].maskId;fails(bad,bg);
    bad=paints;bad[0].opacity=NAN;fails(bad,bg);
    bad=paints;bad[0].color.channels[3]=INFINITY;fails(bad,bg);
    bad=paints;bad[0].color.channels[0]=1.001;fails(bad,bg);
    bad=paints;bad[0].color.alpha=-.1;fails(bad,bg);
    bad=paints;bad[0].color.activeChannels=4;fails(bad,bg);
    bad=paints;bad[0].color.profileId.bytes[0]=1;fails(bad,bg);
    bad=paints;bad.resize(9);fails(bad,bg);
    auto invalid=bg;invalid.format.mode=ColorMode::CMYK;fails(paints,invalid);
    invalid=bg;invalid.format.alpha=false;fails(paints,invalid);
    invalid=bg;invalid.format.depth=static_cast<Depth>(42);fails(paints,invalid);
    invalid=bg;invalid.rowStride=1;fails(paints,invalid);
    invalid=bg;invalid.rowStride=UINT64_MAX;fails(paints,invalid);
    invalid=bg;invalid.bytes.pop_back();fails(paints,invalid);
    fails(paints,bg,{900,900,512,512});fails(paints,bg,{-1,0,512,512});
    fails(paints,bg,{INT64_MAX,0,512,512});fails(paints,bg,{0,0,513,512});
    assert(!renderQtSolidDraftPreview(cache,generation+1,region,paints,bg));
    assert(!renderQtSolidDraftPreview(cache,generation+1,region,{},bg));
    std::stop_source stop;stop.request_stop();
    auto cancelled=renderQtSolidDraftPreview(cache,generation,region,paints,bg,stop.get_token());
    assert(!cancelled && cancelled.error().code==ErrorCode::Cancelled && bg==original);
    auto empty=renderQtSolidDraftPreview(cache,generation,region,{},bg);assert(!empty && empty.error().code==ErrorCode::InvalidInput);
    // Explicit encoded-channel midpoint: not linear-light white/black mixing.
    assert(images[0].constScanLine(500)[400]==255);
    for(auto depth:{Depth::U8,Depth::U16}) {
        auto white=background(1,1,depth);std::fill(white.bytes.begin(),white.bytes.end(),255);
        QtSolidDraftPaint black{1,{},1};black.color.alpha=.5;
        auto half=renderQtSolidDraftPreview(cache,generation,{400,500,1,1},std::span(&black,1),white);
        assert(half && readSample(half.value(),0)==(depth==Depth::U16?32767:127*257));
        black.color.channels={1,0,0,0};std::fill(white.bytes.begin(),white.bytes.end(),0);
        auto straight=renderQtSolidDraftPreview(cache,generation,{400,500,1,1},std::span(&black,1),white);
        assert(straight && readSample(straight.value(),0)==65535);
        assert(readSample(straight.value(),depth==Depth::U16?6:3)==(depth==Depth::U16?32768:128*257));
    }
    assert(cache.stats().rasterCalls==rasters && bg==original);
    std::cout<<"PASS temporary solid draft RGB8/16 encoded-channel compositor; direct/chunk/partition equality, overlap order, straight alpha, errors atomic; no persistence or UI integration\n";
}
