#pragma once
#include "Refinement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
namespace drawborad::refinement {
struct Hotspot { int x{},y{};double score{}; };
struct Evidence { QualityProgress progress; std::vector<Hotspot> hotspots; };
Result<Evidence> collectEvidence(const RefinementRequest&, bool hotspots, std::stop_token);
Result<StrokeEvent> makeDirectionalStroke(const RefinementRequest&, const Hotspot&,
                                           std::uint64_t ordinal,std::uint64_t startUs);
inline double pixelChannel(const TileData& t,std::uint64_t x,std::uint64_t y,unsigned c){
    const auto depth=t.format.depth==Depth::U16?2u:1u;
    const auto offset=std::size_t(y*t.rowStride+(x*t.format.channelCount()+c)*depth);
    if(depth==1)return t.bytes[offset]/255.0;
    return double(std::uint16_t(t.bytes[offset])|
                  (std::uint16_t(t.bytes[offset+1])<<8))/65535.0;
}
inline double luminance(const TileData& t,int x,int y){
    const auto r=pixelChannel(t,std::uint64_t(x),std::uint64_t(y),0);
    const auto g=pixelChannel(t,std::uint64_t(x),std::uint64_t(y),1);
    const auto b=pixelChannel(t,std::uint64_t(x),std::uint64_t(y),2);
    if(t.format.mode==ColorMode::CMYK)return std::clamp(1.0-pixelChannel(t,std::uint64_t(x),std::uint64_t(y),3)-.3*(r+g+b),0.0,1.0);
    return .30*r+.59*g+.11*b;
}
inline bool safeCentre(const RefinementRequest& r,int x,int y,int pad){
    const auto& rect=r.region.bounds;
    if(x<int(rect.x)+pad || y<int(rect.y)+pad ||
       x>=std::int64_t(rect.x)+std::int64_t(rect.width)-pad ||
       y>=std::int64_t(rect.y)+std::int64_t(rect.height)-pad)return false;
    const auto width=r.region.snapshot.spec.pixelSize.width;
    for(int dy=-pad;dy<=pad;++dy)for(int dx=-pad;dx<=pad;++dx){
        const auto offset=(std::uint64_t(y+dy)*width+std::uint64_t(x+dx));
        if(r.region.reviewedMask[std::size_t(offset)]!=255)return false;
    }
    return true;
}
} // namespace drawborad::refinement
