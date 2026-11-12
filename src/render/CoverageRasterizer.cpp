#include "CoverageRasterizer.h"
#include "FineLineCoverage.h"
#include "DryInkCoverage.h"
#include "core/DryInkEvent.h"
#include "core/BucketEvent.h"
#include "core/FreehandEvent.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
namespace {
constexpr std::int64_t Q = 256;
std::int64_t fixed(double v) { return std::llround(v * double(Q)); }
std::int64_t floorQ(std::int64_t x) { return x >= 0 ? x / Q : -((-x + Q - 1) / Q); }
std::uint64_t isqrt(std::uint64_t x) {
    std::uint64_t r = 0, bit = std::uint64_t(1) << 62;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
constexpr std::array<std::array<std::int64_t, 2>, 8> Dir{{
    {32768,0},{23170,23170},{0,32768},{-23170,23170},
    {-32768,0},{-23170,-23170},{0,-32768},{23170,-23170}}};
}
Result<void> validateRasterEvent(const StrokeEvent& e) {
    if (e.engineVersion==8) return validateBucketEvent(e);
    if (e.engineVersion==12) return validateDryInkEvent(e);
    if (e.engineVersion != 1 && e.engineVersion != 2 && e.engineVersion != 10)
        return error(ErrorCode::UnsupportedFeature, "render.engine-version");
    if(e.engineVersion==10){
        auto fine=validateFreehandEvent(e);if(!fine)return fine.error();
    }else if (e.toolId != "pencil" && e.toolId != "colored-pencil" && e.toolId != "fineliner" &&
        e.toolId != "marker" && e.toolId != "highlight" && e.toolId != "guide" &&
        e.toolId != "eraser")
        return error(ErrorCode::UnsupportedFeature, "render.tool");
    const auto& s = e.style;
    if (!std::isfinite(s.width) || s.width <= 0 || s.width > 8192 ||
        !std::isfinite(s.opacity) || s.opacity < 0 || s.opacity > 1 ||
        !std::isfinite(s.hardness) || s.hardness < 0 || s.hardness > 1 || s.angle > 7 ||
        (s.shape != NibShape::Round && s.shape != NibShape::Chisel) ||
        (!s.grain.empty() && s.grain.size() != 64))
        return error(ErrorCode::InvalidInput, "render.footprint");
    if (e.geometry.empty() || e.geometry.size() > 1000000)
        return error(ErrorCode::BudgetExceeded, "render.samples");
    for (const auto& p : e.geometry)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1e9 ||
            std::abs(p.y) > 1e9 || !std::isfinite(p.pressure) || p.pressure < 0 || p.pressure > 1)
            return error(ErrorCode::InvalidInput, "render.coordinate");
    return {};
}
Result<Rect> strokeBounds(const StrokeEvent& e) {
    auto ok = validateRasterEvent(e); if (!ok) return ok.error();
    double x0=e.geometry[0].x,x1=x0,y0=e.geometry[0].y,y1=y0;
    for (const auto& p:e.geometry) { x0=std::min(x0,p.x);x1=std::max(x1,p.x);y0=std::min(y0,p.y);y1=std::max(y1,p.y); }
    const double m=e.style.width*.5+2;
    auto x=std::int64_t(std::floor(x0-m)), y=std::int64_t(std::floor(y0-m));
    return Rect{x,y,std::uint64_t(std::ceil(x1+m)-double(x)),std::uint64_t(std::ceil(y1+m)-double(y))};
}
Result<std::vector<std::uint16_t>> strokeCoverage(const StrokeEvent& e, Rect rect,
                                                  std::uint64_t time, std::stop_token stop) {
    auto ok=validateRasterEvent(e); if(!ok) return ok.error();
    if (!rect.width || !rect.height || rect.width > TileSide || rect.height > TileSide)
        return error(ErrorCode::BudgetExceeded, "render.coverage-region");
    auto pixels=checkedMultiply(rect.width,rect.height); if(!pixels) return pixels.error();
    if (stop.stop_requested()) return error(ErrorCode::Cancelled,"render.cancelled");
    if(e.engineVersion==10)return fineLineCoverage(e,rect,time,stop);
    if(e.engineVersion==12)return dryInkCoverage(e,rect,time,stop);
    std::vector<std::uint16_t> cov(std::size_t(pixels.value()),0);
    if (time < e.startUs) return cov;
    if (e.engineVersion==8) {
        for (std::size_t i=0;i<e.geometry.size();i+=2) {
            if (stop.stop_requested()) return error(ErrorCode::Cancelled,"render.cancelled");
            const auto y=std::int64_t(e.geometry[i].y);
            if (y<rect.y || y>=rect.y+std::int64_t(rect.height)) continue;
            const auto x0=std::max(rect.x,std::int64_t(e.geometry[i].x));
            const auto x1=std::min(rect.x+std::int64_t(rect.width),
                                 std::int64_t(e.geometry[i+1].x));
            if (x0<x1) std::fill_n(cov.begin()+std::size_t(y-rect.y)*rect.width+
                                  std::size_t(x0-rect.x),std::size_t(x1-x0),65535);
        }
        return cov;
    }
    const auto radius=std::max<std::int64_t>(1,fixed(e.style.width*.5));
    const auto hard=std::int64_t(std::llround(e.style.hardness*65535));
    const auto dir=Dir[e.style.angle];
    auto stamp=[&](std::int64_t cx,std::int64_t cy,std::int64_t pressure){
        if(pressure<=0)return;
        auto r=std::max<std::int64_t>(1,(radius*pressure+32767)/65535);
        auto x0=std::max(rect.x,floorQ(cx-r-128)),y0=std::max(rect.y,floorQ(cy-r-128));
        auto x1=std::min(rect.x+std::int64_t(rect.width),floorQ(cx+r+128)+1),
             y1=std::min(rect.y+std::int64_t(rect.height),floorQ(cy+r+128)+1);
        for(auto y=y0;y<y1;y++)for(auto x=x0;x<x1;x++){
            auto dx=x*Q+128-cx,dy=y*Q+128-cy;
            if(e.style.shape==NibShape::Chisel){auto u=(dx*dir[0]+dy*dir[1])/32768;auto v=(-dx*dir[1]+dy*dir[0])/32768;dx=u;dy=v*3;}
            auto dist=std::int64_t(isqrt(std::uint64_t(dx*dx+dy*dy)));
            auto inner=std::max<std::int64_t>(0,r*hard/65535-128),outer=r+128;
            auto a=std::clamp<std::int64_t>((outer-dist)*65535/std::max<std::int64_t>(1,outer-inner),0,65535);
            if(!e.style.grain.empty()){auto ix=unsigned((x%8+8)%8),iy=unsigned((y%8+8)%8);a=a*e.style.grain[iy*8+ix]/255;}
            auto i=std::size_t(y-rect.y)*std::size_t(rect.width)+std::size_t(x-rect.x);
            cov[i]=std::max(cov[i],std::uint16_t(a));
        }
    };
    auto qp=[](double p){return std::int64_t(std::llround(p*65535));};
    if(e.geometry.front().timestampUs<=time) stamp(fixed(e.geometry.front().x),fixed(e.geometry.front().y),qp(e.geometry.front().pressure));
    std::uint64_t work=0;
    for(std::size_t i=1;i<e.geometry.size();i++){
        if(stop.stop_requested())return error(ErrorCode::Cancelled,"render.cancelled");
        const auto& a=e.geometry[i-1];const auto& b=e.geometry[i];if(a.timestampUs>time)break;
        auto ax=fixed(a.x),ay=fixed(a.y),bx=fixed(b.x),by=fixed(b.y),dx=bx-ax,dy=by-ay;
        if(std::abs(dx)>(1ll<<30)||std::abs(dy)>(1ll<<30))return error(ErrorCode::BudgetExceeded,"render.segment-length");
        auto length=isqrt(std::uint64_t(dx*dx+dy*dy));auto spacing=std::uint64_t(std::max<std::int64_t>(64,radius/2));
        auto n=std::max<std::uint64_t>(1,(length+spacing-1)/spacing);if(n>1000000||work+n>2000000)return error(ErrorCode::BudgetExceeded,"render.dab-budget");work+=n;
        auto pa=qp(a.pressure),pb=qp(b.pressure);
        for(std::uint64_t j=1;j<=n;j++){
            long double tick=static_cast<long double>(a.timestampUs)+(static_cast<long double>(b.timestampUs)-a.timestampUs)*j/n;
            if(tick>time)break;
            auto sj=std::int64_t(j),sn=std::int64_t(n);stamp(ax+dx*sj/sn,ay+dy*sj/sn,pa+(pb-pa)*sj/sn);
        }
        if(time<b.timestampUs)break;
    }
    return cov;
}
}
