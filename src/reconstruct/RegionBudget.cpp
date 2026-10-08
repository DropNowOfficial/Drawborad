#include "RefinementInternal.h"
#include <map>
#include <utility>
namespace drawborad::refinement {
namespace {
Result<void> validate(const RefinementRequest& r,std::stop_token stop){
    if(stop.stop_requested() || r.region.stop.stop_requested())
        return error(ErrorCode::Cancelled,"reconstruct.refine-cancelled");
    const auto& source=r.region.source;
    const auto& snap=r.region.snapshot;
    if(r.painted.revision!=snap.revision || r.painted.documentId!=snap.documentId ||
       r.painted.spec!=snap.spec || r.painted.nodes!=snap.nodes)
        return error(ErrorCode::StaleRevision,"reconstruct.refine-revision");
    const auto canvas=snap.spec.pixelSize;
    const auto rect=r.region.bounds;
    if(!source || r.region.objectId.empty() || r.region.layerId.empty() ||
       canvas.width==0 || canvas.height==0 || canvas.width>INT32_MAX || canvas.height>INT32_MAX ||
       rect.x<0 || rect.y<0 || rect.width==0 || rect.height==0 ||
       std::uint64_t(rect.x)>canvas.width || std::uint64_t(rect.y)>canvas.height ||
       rect.width>canvas.width-std::uint64_t(rect.x) ||
       rect.height>canvas.height-std::uint64_t(rect.y) ||
       rect.width*rect.height>32ull*1024ull*1024ull ||
       source->width!=canvas.width || source->height!=canvas.height ||
       source->format!=snap.spec.colorSpec.format ||
       source->profileId!=snap.spec.colorSpec.profileId ||
       !source->format.alpha)
        return error(ErrorCode::InvalidInput,"reconstruct.refine-layout");
    const auto& b=r.budget;
    if(!std::isfinite(b.colorMaeTarget) || !std::isfinite(b.alphaMaeTarget) ||
       b.colorMaeTarget<0 || b.colorMaeTarget>1 || b.alphaMaeTarget<0 || b.alphaMaeTarget>1 ||
       !std::isfinite(b.maxStrokeWidth) || b.maxStrokeWidth<=0 || b.maxStrokeWidth>128 ||
       b.gridStep==0 || b.gridStep>128 || b.maxAdditionalEvents>65536 ||
       b.maxAdditionalSamples>1000000)
        return error(ErrorCode::InvalidInput,"reconstruct.refine-budget");
    const auto size=checkedMultiply(canvas.width,canvas.height);
    if(!size)return size.error();
    if(r.region.reviewedMask.size()!=size.value())
        return error(ErrorCode::InvalidInput,"reconstruct.refine-mask");
    const auto pixelBytes=std::uint64_t(source->format.channelCount())*(source->format.depth==Depth::U16?2u:1u);
    const auto minRow=checkedMultiply(canvas.width,pixelBytes);
    if(!minRow)return minRow.error();
    const auto total=checkedMultiply(source->rowStride,canvas.height);
    if(!total)return total.error();
    if(source->rowStride<minRow.value() || total.value()>source->bytes.size())
        return error(ErrorCode::InvalidInput,"reconstruct.refine-source-layout");
    if(!snap.nodes || !r.painted.eventIndex)
        return error(ErrorCode::InvalidInput,"reconstruct.refine-snapshot");
    const auto object=snap.nodes->find(r.region.objectId);
    const auto layer=snap.nodes->find(r.region.layerId);
    if(object==snap.nodes->end() || layer==snap.nodes->end() ||
       object->second.kind!=NodeKind::Object || layer->second.kind!=NodeKind::Layer ||
       layer->second.objectId!=r.region.objectId || layer->second.locked ||
       object->second.locked || layer->second.sourceKind!=SourceKind::Reconstructed ||
       !layer->second.content.empty() || !layer->second.sourceResource.empty())
        return error(ErrorCode::InvalidInput,"reconstruct.refine-target");
    return {};
}
} // anonymous

Result<Evidence> collectEvidence(const RefinementRequest& req,bool needHotspots,std::stop_token stop){
    auto valid=validate(req,stop);
    if(!valid)return valid.error();
    const auto& src=*req.region.source;
    const auto rect=req.region.bounds;
    const int x0=int(rect.x),y0=int(rect.y);
    const int xend=x0+int(rect.width),yend=y0+int(rect.height);
    const auto width=req.region.snapshot.spec.pixelSize.width;
    const auto colorChannels=src.format.mode==ColorMode::CMYK?4u:3u;
    const auto grid=int(req.budget.gridStep);
    const auto cols=(int(rect.width)+grid-1)/grid;
    const auto rows=(int(rect.height)+grid-1)/grid;
    const auto cells=std::uint64_t(cols)*std::uint64_t(rows);
    if(needHotspots && cells>4ull*1024ull*1024ull)
        return error(ErrorCode::BudgetExceeded,"reconstruct.refine-budget");
    std::vector<Hotspot> winners;
    if(needHotspots)winners.resize(std::size_t(cells));
    ReferenceRenderer renderer;
    double colorSum=0, alphaSum=0, outsideSum=0;
    std::uint64_t inside=0,outside=0,remaining=0;
    const auto sampleStride=src.format.depth==Depth::U16?2u:1u;
    const int pad=std::max(2,int(std::ceil(req.budget.maxStrokeWidth*.5+.5)));
    for(int tileY=y0;tileY<yend;tileY+=int(TileSide)){
        for(int tileX=x0;tileX<xend;tileX+=int(TileSide)){
            if(stop.stop_requested() || req.region.stop.stop_requested())
                return error(ErrorCode::Cancelled,"reconstruct.refine-cancelled");
            const Rect tileRect{tileX,tileY,std::uint64_t(std::min(int(TileSide),xend-tileX)),
                                             std::uint64_t(std::min(int(TileSide),yend-tileY))};
            const EventLimit lim{req.painted.revision,req.painted.eventIndex->size(),UINT64_MAX};
            auto rendered=renderer.renderRegion(req.painted,req.region.layerId,tileRect,lim,stop);
            if(!rendered)return rendered.error();
            const auto& paint=rendered.value();
            for(int y=tileY;y<tileY+int(tileRect.height);++y){
                if((y&63)==0 && (stop.stop_requested() || req.region.stop.stop_requested()))
                    return error(ErrorCode::Cancelled,"reconstruct.refine-cancelled");
                for(int x=tileX;x<tileX+int(tileRect.width);++x){
                    const auto idx=std::size_t(std::uint64_t(y)*width+std::uint64_t(x));
                    const auto m=req.region.reviewedMask[idx]/255.0;
                    const auto px=std::uint64_t(x-tileX),py=std::uint64_t(y-tileY);
                    const auto targetAlpha=pixelChannel(src,std::uint64_t(x),std::uint64_t(y),colorChannels)*m;
                    const auto paintedAlpha=pixelChannel(paint,px,py,colorChannels);
                    if(m==0){outsideSum+=paintedAlpha;++outside;continue;}
                    ++inside;
                    const auto alphaErr=std::abs(targetAlpha-paintedAlpha);
                    alphaSum+=alphaErr;
                    double colorErr=0;
                    if(targetAlpha>1e-8){
                        for(unsigned ch=0;ch<colorChannels;++ch)
                            colorErr+=std::abs(pixelChannel(src,std::uint64_t(x),std::uint64_t(y),ch)-
                                                   pixelChannel(paint,px,py,ch))*targetAlpha;
                        colorErr/=colorChannels;
                    }
                    colorSum+=colorErr;
                    if(alphaErr>req.budget.alphaMaeTarget || colorErr>req.budget.colorMaeTarget)
                        ++remaining;
                    if(needHotspots && safeCentre(req,x,y,pad)){
                        // A discrete painting cell contributes at most one proposed local stroke.
                        const auto cellX=(x-x0)/grid,cellY=(y-y0)/grid;
                        auto& hotspot=winners[std::size_t(cellY)*std::size_t(cols)+std::size_t(cellX)];
                        const double score=std::max(alphaErr,colorErr);
                        if(score>hotspot.score){hotspot={x,y,score};}
                    }
                }
            }
        }
    }
    if(!inside)return error(ErrorCode::InvalidInput,"reconstruct.refine-empty-mask");
    Evidence evidence;
    evidence.progress.evaluatedPixels=inside;
    evidence.progress.remainingPixels=remaining;
    evidence.progress.colorMae=colorSum/double(inside);
    evidence.progress.alphaMae=alphaSum/double(inside);
    evidence.progress.outsideAlphaMae=outside?outsideSum/double(outside):0.0;
    evidence.progress.status=(evidence.progress.colorMae<=req.budget.colorMaeTarget &&
       evidence.progress.alphaMae<=req.budget.alphaMaeTarget &&
       evidence.progress.outsideAlphaMae<=1.0/65535.0 && remaining==0)
       ? QualityStatus::Converged : QualityStatus::NeedsReview;
    if(needHotspots){
        for(const auto& win:winners)if(win.score>std::max(0.005,std::min(req.budget.colorMaeTarget,req.budget.alphaMaeTarget)))
            evidence.hotspots.push_back(win);
        std::stable_sort(evidence.hotspots.begin(),evidence.hotspots.end(),
          [](const Hotspot&a,const Hotspot&b){
           if(a.score!=b.score)return a.score>b.score;
           if(a.y!=b.y)return a.y<b.y;
           return a.x<b.x;
          });
    }
    (void)sampleStride;
    return evidence;
}
} // namespace drawborad::refinement
