#include "PlanningInternal.h"

namespace drawborad {
Result<EventBatch> planBasePigment(const PlanningRegion& region,const StyleSettings& style) {
    const auto checked=planning::checkRegion(region,true);
    if(!checked)return checked.error();
    if(style.toolId!="marker" && style.toolId!="pencil" &&
       style.toolId!="colored-pencil" && style.toolId!="highlight")
        return error(ErrorCode::UnsupportedFeature,"reconstruct.base-tool");
    if(!std::isfinite(style.spacing) || style.spacing<1 || style.spacing>128 ||
       !std::isfinite(style.opacity) || style.opacity<=0 || style.opacity>1)
        return error(ErrorCode::InvalidInput,"reconstruct.base-style");
    auto raster=planning::makeSafe(region,style.width);
    if(!raster)return raster.error();
    const auto& safe=raster.value();
    const auto& source=*region.source;
    double gradientX=0,gradientY=0;
    const auto skip=std::max(1,int(std::max(safe.width,safe.height)/128));
    for(int y=safe.top+1;y+1<safe.top+safe.height;y+=skip)
        for(int x=safe.left+1;x+1<safe.left+safe.width;x+=skip){
            if(!safe.at(x,y))continue;
            gradientX+=std::abs(planning::brightness(source,x+1,y)-
                                planning::brightness(source,x-1,y));
            gradientY+=std::abs(planning::brightness(source,x,y+1)-
                                planning::brightness(source,x,y-1));
        }
    const bool vertical=gradientX>gradientY*1.05;
    const auto step=std::max(1,int(std::lround(style.spacing)));
    const auto minorStart=vertical?safe.left:safe.top;
    const auto minorEnd=vertical?safe.left+safe.width:safe.top+safe.height;
    const auto majorStart=vertical?safe.top:safe.left;
    const auto majorEnd=vertical?safe.top+safe.height:safe.left+safe.width;
    const auto minRun=std::max(3,int(std::ceil(style.width*2)));
    // A 4K region may need hundreds of long strokes, but not a full-size
    // geometry sample for every four source pixels. Allocate a deterministic
    // per-sweep sample allowance before creating events; never reduce the
    // document resolution or substitute a bitmap for the resulting strokes.
    const auto sweeps=std::uint64_t((minorEnd-minorStart+step-1)/step);
    const auto perSweep=std::max<std::uint64_t>(2,region.maxSamples/std::max<std::uint64_t>(1,sweeps));
    const auto pathStep=std::max(4,int(std::ceil(double(majorEnd-majorStart)/
                                            double(std::max<std::uint64_t>(1,perSweep-2)))));
    StrokeStyle brush;
    brush.width=style.width;brush.opacity=style.opacity;brush.hardness=1;
    EventBatch batch{region.snapshot.revision,region.objectId,region.layerId,{}};
    std::uint64_t totalSamples=0,time=100;
    for(int minor=minorStart+step/2;minor<minorEnd;minor+=step){
        if(region.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        int major=majorStart;
        while(major<majorEnd){
            const auto at=[&](int m){return vertical?safe.at(minor,m):safe.at(m,minor);};
            while(major<majorEnd && !at(major))++major;
            const int begin=major;
            while(major<majorEnd && at(major))++major;
            const int end=major;
            if(end-begin<minRun)continue;
            std::vector<std::pair<int,int>> path;
            const auto append=[&](int m){path.emplace_back(vertical?minor:m,vertical?m:minor);};
            append(begin);
            for(int m=begin+pathStep;m+1<end;m+=pathStep)append(m);
            append(end-1);
            const int center=(begin+end-1)/2;
            const int cx=vertical?minor:center,cy=vertical?center:minor;
            auto added=planning::addEvent(batch,region,brush,style.toolId,
                         planning::colorAt(region,cx,cy),path,2,totalSamples,time);
            if(!added)return added.error();
        }
    }
    if(batch.events.empty())
        return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    return validateReconstructionBatch(batch);
}
} // namespace drawborad
