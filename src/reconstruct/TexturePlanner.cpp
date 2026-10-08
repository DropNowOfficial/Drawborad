#include "RefinementInternal.h"
namespace drawborad::refinement {
Result<StrokeEvent> makeDirectionalStroke(const RefinementRequest& r,const Hotspot& spot,
                                           std::uint64_t ordinal,std::uint64_t startUs){
    const auto& src=*r.region.source;
    const int x=spot.x,y=spot.y;
    const int maxX=int(src.width)-2,maxY=int(src.height)-2;
    const auto x0=std::clamp(x-1,0,maxX),x1=std::clamp(x+1,1,int(src.width)-1);
    const auto y0=std::clamp(y-1,0,maxY),y1=std::clamp(y+1,1,int(src.height)-1);
    const auto gx=std::abs(luminance(src,x1,y)-luminance(src,x0,y));
    const auto gy=std::abs(luminance(src,x,y1)-luminance(src,x,y0));
    // A narrow fold follows the local isophote, not an image-wide fixed hatch direction.
    const bool horizontal=gy>gx;
    const int ax=horizontal?x-1:x,ay=horizontal?y:y-1;
    const int bx=horizontal?x+1:x,by=horizontal?y:y+1;
    const int pad=std::max(2,int(std::ceil(r.budget.maxStrokeWidth*.5+.5)));
    if(!safeCentre(r,ax,ay,pad) || !safeCentre(r,bx,by,pad))
        return error(ErrorCode::InvalidInput,"reconstruct.refine-unsafe-stroke");
    StrokeEvent e;
    // High half of the ID domain is intentionally disjoint from E02 planning IDs.
    const std::uint64_t hi=0x4530335f52454649ull ^ r.region.seed ^
             std::uint64_t(r.painted.eventIndex->size());
    for(int i=0;i<8;++i){e.id.bytes[std::size_t(i)]=std::uint8_t(hi>>(8*(7-i)));
                         e.id.bytes[std::size_t(i+8)]=std::uint8_t((ordinal+1)>>(8*(7-i)));}
    e.objectId=r.region.objectId;e.layerId=r.region.layerId;
    e.parentRevision=r.region.snapshot.revision;e.engineVersion=2;
    e.toolId="colored-pencil";
    e.seed=r.region.seed;
    e.style.width=std::min(2.0,r.budget.maxStrokeWidth);
    e.style.opacity=1;e.style.hardness=1;
    e.color.profileId=src.profileId;
    const unsigned colors=src.format.mode==ColorMode::CMYK?4u:3u;
    e.color.activeChannels=std::uint8_t(colors);
    for(unsigned ch=0;ch<colors;++ch)e.color.channels[ch]=pixelChannel(src,std::uint64_t(x),std::uint64_t(y),ch);
    e.color.alpha=pixelChannel(src,std::uint64_t(x),std::uint64_t(y),colors);
    e.startUs=startUs;e.endUs=startUs+100;
    e.geometry.push_back({double(ax)+.5,double(ay)+.5,1.0,{}, {},startUs,0,0});
    e.geometry.push_back({double(bx)+.5,double(by)+.5,1.0,{}, {},startUs+100,0,0});
    return e;
}
} // namespace drawborad::refinement
