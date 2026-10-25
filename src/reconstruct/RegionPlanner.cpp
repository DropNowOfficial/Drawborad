#include "PlanningInternal.h"
#include <functional>

namespace drawborad {
Result<EventBatch> planBasePigment(const PlanningRegion& region,const StyleSettings& style) {
    const auto checked=planning::checkRegion(region,true);
    if(!checked)return checked.error();
    if(style.toolId!="marker" && style.toolId!="pencil" &&
       style.toolId!="colored-pencil" && style.toolId!="highlight")
        return error(ErrorCode::UnsupportedFeature,"reconstruct.base-tool");
    if(!std::isfinite(style.spacing) || style.spacing<1 || style.spacing>128 ||
       !std::isfinite(style.opacity) || style.opacity<=0 || style.opacity>1 ||
       !std::isfinite(style.maxColorVariation) || style.maxColorVariation<0 || style.maxColorVariation>1)
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
    auto covered=safe;std::fill(covered.pixels.begin(),covered.pixels.end(),std::uint8_t{0});
    // The second pass only visits visible fragments outside the conservative
    // opaque core of the long sweep strokes. It fixes thin disconnected mask
    // edges without printing a per-pixel residual image.
    for(int pass=0;pass<(region.clipToReviewedMask?2:1);++pass)
    for(int minor=pass?minorStart:minorStart+step/2;minor<minorEnd;minor+=pass?1:step){
        if(region.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
        int major=majorStart;
        while(major<majorEnd){
            const auto at=[&](int m){const int x=vertical?minor:m,y=vertical?m:minor;
                return safe.at(x,y) && (!pass || !covered.at(x,y));};
            while(major<majorEnd && !at(major))++major;
            const int begin=major;
            while(major<majorEnd && at(major))++major;
            const int end=major;
            if(end==begin)continue;
            // A narrow visible fragment still needs a connected stroke or a
            // single nib dab. Adaptive color splitting keeps the longer-run
            // minimum; this does not turn the image into per-pixel events.
            if(end-begin<minRun && !region.clipToReviewedMask)continue;
            std::function<Result<void>(int,int)> emitRun=[&](int first,int last)->Result<void>{
                if(region.stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.planning-cancelled");
                const int center=(first+last-1)/2;
                auto color=planning::colorAt(region,vertical?minor:center,vertical?center:minor);
                if(style.maxColorVariation>0){
                    std::array<double,4> sum{};
                    for(int m=first;m<last;++m){
                        const auto sample=planning::colorAt(region,vertical?minor:m,vertical?m:minor);
                        for(unsigned c=0;c<color.activeChannels;++c)sum[c]+=sample.channels[c];
                    }
                    for(unsigned c=0;c<color.activeChannels;++c)color.channels[c]=sum[c]/double(last-first);
                    double variation=0,boundaryScore=0;int split=(first+last)/2;
                    auto previous=planning::colorAt(region,vertical?minor:first,vertical?first:minor);
                    for(int m=first;m<last;++m){
                        const auto sample=planning::colorAt(region,vertical?minor:m,vertical?m:minor);
                        double edge=0;
                        for(unsigned c=0;c<color.activeChannels;++c){
                            variation=std::max(variation,std::abs(sample.channels[c]-color.channels[c]));
                            edge=std::max(edge,std::abs(sample.channels[c]-previous.channels[c]));
                        }
                        if(m-first>=minRun && last-m>=minRun && edge>boundaryScore){boundaryScore=edge;split=m;}
                        previous=sample;
                    }
                    if(variation>style.maxColorVariation && last-first>=2*minRun){
                        if(boundaryScore<style.maxColorVariation*.5)split=(first+last)/2;
                        auto left=emitRun(first,split);if(!left)return left.error();return emitRun(split,last);
                    }
                }
                std::vector<std::pair<int,int>> path;
                const auto append=[&](int m){path.emplace_back(vertical?minor:m,vertical?m:minor);};
                append(first);for(int m=first+pathStep;m+1<last;m+=pathStep)append(m);append(last-1);
                auto added=planning::addEvent(batch,region,brush,style.toolId,color,path,2,totalSamples,time);
                if(!added)return added.error();
                if(region.clipToReviewedMask){
                    const auto core=std::clamp(int(std::floor(style.width*.5-1)),0,8);
                    for(int n=minor-core;n<=minor+core;++n)for(int m=first-core;m<last+core;++m){
                        const auto beyond=std::max({first-m,0,m-(last-1)});
                        if(beyond+std::abs(n-minor)>core)continue;
                        const int x=vertical?n:m,y=vertical?m:n;
                        if(x>=covered.left && y>=covered.top && x<covered.left+covered.width && y<covered.top+covered.height)
                            covered.pixels[std::size_t(y-covered.top)*std::size_t(covered.width)+std::size_t(x-covered.left)]=1;
                    }
                }
                return {};
            };
            auto added=emitRun(begin,end);
            if(!added)return added.error();
        }
    }
    if(batch.events.empty())
        return error(ErrorCode::InvalidInput,"reconstruct.empty-safe-region");
    return validateReconstructionBatch(batch);
}
} // namespace drawborad
