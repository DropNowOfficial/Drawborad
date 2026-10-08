#include "Planning.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

namespace drawborad {
namespace {
bool contains(Rect r,double x,double y){
    return r.x>=0 && r.y>=0 && x>=r.x && y>=r.y &&
           x<static_cast<double>(r.x)+r.width &&
           y<static_cast<double>(r.y)+r.height;
}
Id orderedId(const SchedulePolicy& policy,std::size_t index){
    // The high half identifies a deterministic planning run; the low half
    // is an ordinal, ensuring IDs are distinct even if source IDs collide.
    Id id;
    const auto header=(policy.seed^0x454f325f504c414eull);
    for(int i=0;i<8;++i){id.bytes[i]=std::uint8_t(header>>(8*(7-i)));
                          id.bytes[i+8]=std::uint8_t(std::uint64_t(index+1)>>(8*(7-i)));}
    return id;
}
}
Result<EventBatch> scheduleStrokes(const EventBatch& input,const SchedulePolicy& policy){
    if(input.events.empty() || input.objectId.empty() || input.layerId.empty())
        return error(ErrorCode::InvalidInput,"reconstruct.schedule-empty");
    if(input.events.size()>policy.maxEvents || policy.maxEvents>65536 ||
       policy.sampleStepUs==0 || policy.gapUs==0)
        return error(ErrorCode::BudgetExceeded,"reconstruct.schedule-budget");
    std::uint64_t count=0;
    for(const auto& event:input.events){
        if(event.geometry.empty() || event.geometry.size()>1000000 ||
           event.objectId!=input.objectId || event.layerId!=input.layerId ||
           event.parentRevision!=input.inputRevision)
            return error(ErrorCode::InvalidInput,"reconstruct.schedule-event");
        count+=event.geometry.size();
    }
    const auto max=std::numeric_limits<std::uint64_t>::max();
    if(count>max/policy.sampleStepUs ||
       input.events.size()> (max-policy.startUs-count*policy.sampleStepUs)/policy.gapUs)
        return error(ErrorCode::BudgetExceeded,"reconstruct.schedule-time");
    std::vector<std::size_t> regionIndices(input.events.size(),0);
    for(std::size_t i=0;i<input.events.size();++i){
        if(policy.regionOrder.empty())continue;
        const auto& start=input.events[i].geometry.front();
        auto found=policy.regionOrder.size();
        for(std::size_t j=0;j<policy.regionOrder.size();++j){
            if(contains(policy.regionOrder[j],start.x,start.y)){found=j;break;}
        }
        if(found==policy.regionOrder.size())
            return error(ErrorCode::InvalidInput,"reconstruct.schedule-region");
        // A region-local scheduling claim must hold for the entire stroke, not
        // only its starting point. Rectangles are convex, so each segment
        // remains inside when all control points do.
        for(const auto& point:input.events[i].geometry){
            if(!contains(policy.regionOrder[found],point.x,point.y))
                return error(ErrorCode::InvalidInput,"reconstruct.schedule-region");
        }
        regionIndices[i]=found;
    }
    std::vector<std::size_t> order(input.events.size());
    std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
        const bool aGuide=input.events[a].toolId=="guide";
        const bool bGuide=input.events[b].toolId=="guide";
        return std::tuple{regionIndices[a],!aGuide,a}<
               std::tuple{regionIndices[b],!bGuide,b};
    });
    EventBatch output{input.inputRevision,input.objectId,input.layerId,{}};
    output.events.reserve(order.size());
    auto time=policy.startUs;
    for(const auto i:order){
        StrokeEvent event=input.events[i];
        event.id=orderedId(policy,output.events.size());
        event.startUs=time;
        for(auto& point:event.geometry){point.timestampUs=time;time+=policy.sampleStepUs;}
        event.endUs=event.geometry.back().timestampUs;
        time+=policy.gapUs;
        output.events.push_back(std::move(event));
    }
    return validateReconstructionBatch(output);
}
} // namespace drawborad
