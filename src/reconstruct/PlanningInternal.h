#pragma once
#include "Planning.h"
#include "render/ReferenceRenderer.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace drawborad::planning {

struct SafeRaster {
    int left{}, top{}, width{}, height{};
    std::vector<std::uint8_t> pixels;
    bool at(int x, int y) const {
        const int rx=x-left, ry=y-top;
        return rx>=0 && ry>=0 && rx<width && ry<height &&
               pixels[std::size_t(ry)*std::size_t(width)+std::size_t(rx)]!=0;
    }
};

inline Result<void> checkRegion(const PlanningRegion& region,bool needSource) {
    if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
    const auto size=region.snapshot.spec.pixelSize;
    if(size.width==0 || size.height==0 || size.width>INT32_MAX || size.height>INT32_MAX ||
       region.bounds.x<0 || region.bounds.y<0 || !region.bounds.width || !region.bounds.height ||
       std::uint64_t(region.bounds.x)>size.width || std::uint64_t(region.bounds.y)>size.height ||
       region.bounds.width>size.width-std::uint64_t(region.bounds.x) ||
       region.bounds.height>size.height-std::uint64_t(region.bounds.y) ||
       region.bounds.width>INT32_MAX || region.bounds.height>INT32_MAX ||
       region.objectId.empty() || region.layerId.empty() ||
       !region.snapshot.spec.colorSpec.format.alpha)
        return error(ErrorCode::InvalidInput,"reconstruct.planning-region");
    const auto total=checkedMultiply(size.width,size.height);
    if(!total)return total.error();
    const auto area=checkedMultiply(region.bounds.width,region.bounds.height);
    if(!area)return area.error();
    if(area.value()>32ull*1024*1024 || region.maxEvents==0 || region.maxEvents>65536 ||
       region.maxSamples<2 || region.maxSamples>1000000)
        return error(ErrorCode::BudgetExceeded,"reconstruct.planning-budget");
    if(total.value()!=region.reviewedMask.size())
        return error(ErrorCode::InvalidInput,"reconstruct.planning-mask-size");
    if(region.clipToReviewedMask){
        const auto object=region.snapshot.nodes->find(region.objectId);
        const auto layer=region.snapshot.nodes->find(region.layerId);
        if(object==region.snapshot.nodes->end() || object->second.kind!=NodeKind::Object ||
           layer==region.snapshot.nodes->end() || layer->second.objectId!=region.objectId ||
           region.bounds!=Rect{0,0,size.width,size.height} ||
           !nativeLayerPaintable(region.snapshot,region.layerId))
            return error(ErrorCode::InvalidInput,"reconstruct.planning-mask-clipping");
        const auto mask=region.snapshot.resources->find(object->second.maskId);
        if(mask==region.snapshot.resources->end() || mask->second.kind!=ResourceKind::Mask ||
           mask->second.bytes->size()!=region.reviewedMask.size() ||
           !std::equal(mask->second.bytes->begin(),mask->second.bytes->end(),region.reviewedMask.begin()) ||
           !std::all_of(region.reviewedMask.begin(),region.reviewedMask.end(),[](auto a){return a==0 || a==255;}))
            return error(ErrorCode::InvalidInput,"reconstruct.planning-mask-clipping");
    }
    if(needSource){
        const auto* source=region.source;
        if(!source || source->width!=size.width || source->height!=size.height ||
           source->format!=region.snapshot.spec.colorSpec.format ||
           source->profileId!=region.snapshot.spec.colorSpec.profileId)
            return error(ErrorCode::InvalidInput,"reconstruct.planning-source-layout");
        const auto row=checkedMultiply(size.width,std::uint64_t(source->format.channelCount())*
                                        (source->format.depth==Depth::U16?2u:1u));
        if(!row)return row.error();
        const auto bytes=checkedMultiply(source->rowStride,size.height);
        if(!bytes)return bytes.error();
        if(source->rowStride<row.value() || bytes.value()>source->bytes.size())
            return error(ErrorCode::InvalidInput,"reconstruct.planning-source-layout");
    }
    return {};
}

inline Result<SafeRaster> makeSafe(const PlanningRegion& region,double strokeWidth) {
    if(!std::isfinite(strokeWidth) || strokeWidth<=0 || strokeWidth>128)
        return error(ErrorCode::InvalidInput,"reconstruct.planning-width");
    const auto base=checkRegion(region,false);
    if(!base)return base.error();
    const int w=int(region.bounds.width),h=int(region.bounds.height);
    const int left=int(region.bounds.x),top=int(region.bounds.y);
    if(region.clipToReviewedMask){
        SafeRaster safe{left,top,w,h,std::vector<std::uint8_t>(region.reviewedMask.begin(),region.reviewedMask.end())};
        return safe;
    }
    const int pad=int(std::ceil(strokeWidth*.5+.5)); // Round nib + 0.5px coverage falloff.
    const std::size_t stride=std::size_t(w)+1;
    std::vector<std::uint32_t> prefix((std::size_t(h)+1)*stride,0);
    const auto docW=region.snapshot.spec.pixelSize.width;
    for(int y=0;y<h;++y){
        if((y&127)==0 && region.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        for(int x=0;x<w;++x){
            const auto masked=region.reviewedMask[(std::uint64_t(y+top)*docW)+std::uint64_t(x+left)];
            const auto i=(std::size_t(y)+1)*stride+std::size_t(x+1);
            prefix[i]=prefix[i-stride]+prefix[i-1]-prefix[i-stride-1]+(masked==255?0u:1u);
        }
    }
    SafeRaster safe{left,top,w,h,std::vector<std::uint8_t>(std::size_t(w)*std::size_t(h))};
    for(int y=pad;y<h-pad;++y)for(int x=pad;x<w-pad;++x){
        const auto x0=std::size_t(x-pad),x1=std::size_t(x+pad+1);
        const auto y0=std::size_t(y-pad),y1=std::size_t(y+pad+1);
        const auto blocked=prefix[y1*stride+x1]-prefix[y0*stride+x1]-
                           prefix[y1*stride+x0]+prefix[y0*stride+x0];
        if(blocked==0)safe.pixels[std::size_t(y)*std::size_t(w)+std::size_t(x)]=1;
    }
    return safe;
}

inline Id eventId(const PlanningRegion& r,std::uint32_t kind,std::uint64_t ordinal){
    std::uint64_t hash=1469598103934665603ull;
    const auto mix=[&](std::uint8_t x){hash=(hash^x)*1099511628211ull;};
    for(auto b:r.objectId.bytes)mix(b);
    for(auto b:r.layerId.bytes)mix(b);
    for(int i=0;i<8;++i)mix(std::uint8_t(r.seed>>(8*i)));
    mix(std::uint8_t(kind));
    Id id;
    for(int i=0;i<8;++i){id.bytes[i]=std::uint8_t(hash>>(8*(7-i)));
                          id.bytes[i+8]=std::uint8_t((ordinal+1)>>(8*(7-i)));}
    return id;
}
inline ColorValue contourColor(const PlanningRegion& r){
    ColorValue c;
    c.profileId=r.snapshot.spec.colorSpec.profileId;
    c.activeChannels=r.snapshot.spec.colorSpec.format.mode==ColorMode::CMYK?4:3;
    if(c.activeChannels==4)c.channels={0.0,0.0,0.0,0.70};
    else c.channels={0.28,0.28,0.28,0};
    c.alpha=1;
    return c;
}
inline double sampleChannel(const TileData& source,int x,int y,int channel){
    const auto depth=source.format.depth==Depth::U16?2u:1u;
    const auto at=std::size_t(y)*std::size_t(source.rowStride)+
                  (std::size_t(x)*source.format.channelCount()+std::size_t(channel))*depth;
    if(depth==1)return double(source.bytes[at])/255.0;
    return double(std::uint16_t(source.bytes[at])|
                  (std::uint16_t(source.bytes[at+1])<<8))/65535.0;
}
inline ColorValue colorAt(const PlanningRegion& r,int x,int y){
    const auto& s=*r.source;
    ColorValue c;
    c.profileId=s.profileId;
    c.activeChannels=s.format.mode==ColorMode::CMYK?4:3;
    for(unsigned i=0;i<c.activeChannels;++i)c.channels[i]=sampleChannel(s,x,y,int(i));
    c.alpha=s.format.alpha?sampleChannel(s,x,y,c.activeChannels):1.0;
    return c;
}
inline double brightness(const TileData& s,int x,int y){
    const auto r=sampleChannel(s,x,y,0),g=sampleChannel(s,x,y,1),b=sampleChannel(s,x,y,2);
    if(s.format.mode==ColorMode::CMYK)
        return std::clamp(1.0-sampleChannel(s,x,y,3)-.3*(r+g+b),0.0,1.0);
    return .3*r+.59*g+.11*b;
}
inline Result<void> addEvent(EventBatch& batch,const PlanningRegion& region,
                             const StrokeStyle& style,std::string_view tool,
                             ColorValue color,const std::vector<std::pair<int,int>>& path,
                             std::uint32_t kind,std::uint64_t& sampleCount,
                             std::uint64_t& time){
    if(path.size()<2)return {};
    if(batch.events.size()>=region.maxEvents || path.size()>region.maxSamples-sampleCount)
        return error(ErrorCode::BudgetExceeded,"reconstruct.planning-budget");
    StrokeEvent e;
    e.id=eventId(region,kind,batch.events.size());
    e.objectId=region.objectId;e.layerId=region.layerId;
    e.parentRevision=region.snapshot.revision;e.engineVersion=2;
    e.toolId=std::string(tool);e.color=std::move(color);e.seed=region.seed;
    e.style=style;
    e.startUs=time;
    for(const auto& [x,y]:path){
        e.geometry.push_back({double(x)+.5,double(y)+.5,1.0,{}, {},time,0,0});
        time+=100;
    }
    e.endUs=e.geometry.back().timestampUs;
    time+=100;
    sampleCount+=path.size();
    batch.events.push_back(std::move(e));
    return {};
}
} // namespace drawborad::planning
