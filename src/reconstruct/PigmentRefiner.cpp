#include "RefinementInternal.h"
#include <limits>
namespace drawborad {
Result<QualityProgress> measureRegionProgress(const RefinementRequest& r){
    auto evidence=refinement::collectEvidence(r,false,r.stop);
    if(!evidence)return evidence.error();
    return evidence.value().progress;
}
Result<EventBatch> refineRegion(const RefinementRequest& r,std::stop_token stop){
    if(stop.stop_requested() || r.stop.stop_requested())
        return error(ErrorCode::Cancelled,"reconstruct.refine-cancelled");
    const auto evidence=refinement::collectEvidence(r,true,stop);
    if(!evidence)return evidence.error();
    EventBatch batch{r.region.snapshot.revision,r.region.objectId,r.region.layerId,{}};
    if(evidence.value().progress.status==QualityStatus::Converged)return batch;
    const auto available=std::min(r.budget.maxAdditionalEvents,r.budget.maxAdditionalSamples/2u);
    if(available==0 || evidence.value().hotspots.empty())
        return error(ErrorCode::BudgetExceeded,"reconstruct.needs-review");
    const auto& events=*r.painted.eventIndex;
    std::uint64_t last=0;
    for(const auto& e:events){last=std::max(last,e.endUs);}
    if(last>std::numeric_limits<std::uint64_t>::max()-2*available*300-100)
        return error(ErrorCode::BudgetExceeded,"reconstruct.refine-time-budget");
    auto at=last+100;
    for(const auto& spot:evidence.value().hotspots){
        if(stop.stop_requested() || r.stop.stop_requested())
            return error(ErrorCode::Cancelled,"reconstruct.refine-cancelled");
        if(batch.events.size()>=available)break;
        auto stroke=refinement::makeDirectionalStroke(r,spot,batch.events.size(),at);
        if(!stroke)continue;
        batch.events.push_back(std::move(stroke.value()));
        at+=300;
    }
    if(batch.events.empty())return error(ErrorCode::BudgetExceeded,"reconstruct.needs-review");
    return batch;
}
} // namespace drawborad
