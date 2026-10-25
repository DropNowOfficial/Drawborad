#include "ReviewSession.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace drawborad {
namespace {
void recomputeUnassigned(MaskSet& masks) {
    const auto pixels = std::size_t(masks.dimensions.width * masks.dimensions.height);
    masks.unassigned.assign(pixels, 255);
    for (const auto& mask : masks.fullResolutionMasks) {
        if (mask.size() != pixels)
            continue;
        for (std::size_t i = 0; i < pixels; ++i)
            if (mask[i] > 0)
                masks.unassigned[i] = 0;
    }
}

Result<Bytes> transformedMask(const Bytes& source, PixelSize size, const LayerTransform& t) {
    if (source.size() != size.width * size.height)
        return error(ErrorCode::CorruptData, "review.mask-layout");
    const auto a=t.affine[0], b=t.affine[1], c=t.affine[2], d=t.affine[3], tx=t.affine[4], ty=t.affine[5];
    const auto det=a*d-b*c;
    if (!std::isfinite(det) || std::abs(det) < 1e-12)
        return error(ErrorCode::InvalidInput, "review.transform-singular");
    for (auto v : t.affine)
        if (!std::isfinite(v))
            return error(ErrorCode::InvalidInput, "review.transform-nonfinite");

    Bytes out(source.size(), 0);
    for (std::uint64_t y=0; y<size.height; ++y) {
        for (std::uint64_t x=0; x<size.width; ++x) {
            const double px = double(x) + 0.5 - tx;
            const double py = double(y) + 0.5 - ty;
            const double sx = ( d*px - c*py) / det - 0.5;
            const double sy = (-b*px + a*py) / det - 0.5;
            const auto ix = std::int64_t(std::llround(sx));
            const auto iy = std::int64_t(std::llround(sy));
            if (ix < 0 || iy < 0 || ix >= std::int64_t(size.width) || iy >= std::int64_t(size.height))
                continue;
            out[std::size_t(y*size.width+x)] = source[std::size_t(std::uint64_t(iy)*size.width+std::uint64_t(ix))];
        }
    }
    return out;
}
}

Result<ReviewSession> ReviewSession::create(const ProposalSet& p,const MaskSet& m,Revision r,RefineRunner refineRunner){
    if(p.candidates.size()!=m.fullResolutionMasks.size())
        return error(ErrorCode::InvalidInput,"review.mask-count");
    const auto pixels=m.dimensions.width*m.dimensions.height;
    if(!m.dimensions.width||!m.dimensions.height)
        return error(ErrorCode::InvalidInput,"review.mask-size");
    for(const auto& mask:m.fullResolutionMasks)
        if(mask.size()!=pixels)
            return error(ErrorCode::CorruptData,"review.mask-layout");
    ReviewSession s;
    s.proposals_=p;
    s.masks_=m;
    recomputeUnassigned(s.masks_);
    s.revision_=r;
    s.locked_.assign(m.fullResolutionMasks.size(),false);
    s.refineRunner_=std::move(refineRunner);
    return s;
}

Result<void> ReviewSession::apply(const MaskEdit& e){
    if(e.expectedRevision!=revision_)
        return error(ErrorCode::StaleRevision,"review.revision");
    if(e.objectIndex>=masks_.fullResolutionMasks.size())
        return error(ErrorCode::InvalidInput,"review.object");
    if(locked_[e.objectIndex]&&e.operation!=MaskEditOp::Unlock)
        return error(ErrorCode::Locked,"review.mask-locked");

    if(e.operation==MaskEditOp::Merge){
        if(e.secondaryIndex>=masks_.fullResolutionMasks.size()||e.secondaryIndex==e.objectIndex)
            return error(ErrorCode::InvalidInput,"review.merge-object");
        if(locked_[e.secondaryIndex])
            return error(ErrorCode::Locked,"review.merge-locked");
    }
    if(e.operation==MaskEditOp::SubtractMask && (e.secondaryIndex>=masks_.fullResolutionMasks.size() || e.secondaryIndex==e.objectIndex))
        return error(ErrorCode::InvalidInput,"review.subtract-object");

    undo_.push_back({proposals_,masks_,locked_});

    if(e.operation==MaskEditOp::Lock){
        locked_[e.objectIndex]=true;
        revision_++;
        return{};
    }
    if(e.operation==MaskEditOp::Unlock){
        locked_[e.objectIndex]=false;
        revision_++;
        return{};
    }

    auto& m=masks_.fullResolutionMasks[e.objectIndex];
    const auto w=masks_.dimensions.width,h=masks_.dimensions.height;
    if(m.size()!=w*h)
        return error(ErrorCode::CorruptData,"review.mask-layout");

    auto x0=std::max<std::int64_t>(0,e.region.x);
    auto y0=std::max<std::int64_t>(0,e.region.y);
    auto x1=std::min<std::uint64_t>(w,std::uint64_t(x0)+e.region.width);
    auto y1=std::min<std::uint64_t>(h,std::uint64_t(y0)+e.region.height);

    if(e.operation==MaskEditOp::AddBrush||e.operation==MaskEditOp::SubtractBrush){
        const auto v=e.operation==MaskEditOp::AddBrush?std::uint8_t(255):std::uint8_t(0);
        for(std::uint64_t y=y0;y<y1;y++)
            for(std::uint64_t x=x0;x<x1;x++)
                m[std::size_t(y*w+x)]=v;
    } else if(e.operation==MaskEditOp::Grow||e.operation==MaskEditOp::Shrink){
        Bytes before=m;
        int rad=int(std::clamp(e.radiusPx,1.0,64.0));
        for(std::uint64_t y=0;y<h;y++)for(std::uint64_t x=0;x<w;x++){
            bool hit=e.operation==MaskEditOp::Shrink;
            for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){
                auto xx=std::int64_t(x)+dx,yy=std::int64_t(y)+dy;
                if(xx<0||yy<0||xx>=std::int64_t(w)||yy>=std::int64_t(h)) {
                    if(e.operation==MaskEditOp::Shrink) hit=false;
                    continue;
                }
                const bool on=before[std::size_t(std::uint64_t(yy)*w+std::uint64_t(xx))]>127;
                if(e.operation==MaskEditOp::Grow) hit=hit||on;
                else hit=hit&&on;
            }
            m[std::size_t(y*w+x)]=hit?255:0;
        }
    } else if(e.operation==MaskEditOp::Feather){
        Bytes before=m;
        for(std::uint64_t y=0;y<h;y++)for(std::uint64_t x=0;x<w;x++){
            unsigned sum=0,n=0;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                auto xx=std::int64_t(x)+dx,yy=std::int64_t(y)+dy;
                if(xx>=0&&yy>=0&&xx<std::int64_t(w)&&yy<std::int64_t(h)){
                    sum+=before[std::size_t(std::uint64_t(yy)*w+std::uint64_t(xx))];
                    n++;
                }
            }
            m[std::size_t(y*w+x)]=std::uint8_t(sum/n);
        }
    } else if(e.operation==MaskEditOp::Transform){
        auto moved=transformedMask(m,masks_.dimensions,e.transform);
        if(!moved){
            undo_.pop_back();
            return moved.error();
        }
        m=std::move(moved.value());
    } else if(e.operation==MaskEditOp::Merge){
        auto& other=masks_.fullResolutionMasks[e.secondaryIndex];
        if(other.size()!=m.size()){
            undo_.pop_back();
            return error(ErrorCode::CorruptData,"review.mask-layout");
        }
        for(std::size_t i=0;i<m.size();++i)
            m[i]=std::max(m[i],other[i]);
        const auto eraseIndex=e.secondaryIndex;
        masks_.fullResolutionMasks.erase(masks_.fullResolutionMasks.begin()+std::ptrdiff_t(eraseIndex));
        locked_.erase(locked_.begin()+std::ptrdiff_t(eraseIndex));
        if(eraseIndex<proposals_.candidates.size())
            proposals_.candidates.erase(proposals_.candidates.begin()+std::ptrdiff_t(eraseIndex));
    } else if(e.operation==MaskEditOp::SubtractMask){
        const auto& other=masks_.fullResolutionMasks[e.secondaryIndex];
        for(std::size_t i=0;i<m.size();++i)m[i]=std::uint8_t((unsigned(m[i])*(255u-other[i])+127)/255);
    } else if(e.operation==MaskEditOp::Split){
        if(!e.region.width||!e.region.height){
            undo_.pop_back();
            return error(ErrorCode::InvalidInput,"review.split-region");
        }
        Bytes split(m.size(),0);
        bool movedAny=false;
        for(std::uint64_t y=y0;y<y1;y++)for(std::uint64_t x=x0;x<x1;x++){
            const auto idx=std::size_t(y*w+x);
            if(m[idx]){
                split[idx]=m[idx];
                m[idx]=0;
                movedAny=true;
            }
        }
        if(!movedAny){
            undo_.pop_back();
            return error(ErrorCode::InvalidInput,"review.split-empty");
        }
        masks_.fullResolutionMasks.push_back(std::move(split));
        locked_.push_back(false);
        ProposalCandidate candidate;
        candidate.id=Id::fromNumber(std::uint64_t(proposals_.candidates.size()+1));
        candidate.bounds=e.region;
        candidate.score=0;
        candidate.lowResMask.assign(256*256,0);
        proposals_.candidates.push_back(std::move(candidate));
    } else {
        undo_.pop_back();
        return error(ErrorCode::UnsupportedFeature,"review.operation");
    }

    recomputeUnassigned(masks_);
    revision_++;
    return{};
}

Result<void> ReviewSession::appendProposals(const ProposalSet& proposals,const MaskSet& masks,Revision expected,bool prepend){
    if(expected!=revision_)return error(ErrorCode::StaleRevision,"review.revision");
    const auto pixels=checkedMultiply(masks_.dimensions.width,masks_.dimensions.height);
    if(!pixels || masks.dimensions!=masks_.dimensions || proposals.sourceSize!=masks_.dimensions ||
       proposals.candidates.empty() || proposals.candidates.size()!=masks.fullResolutionMasks.size())return error(ErrorCode::InvalidInput,"review.append-layout");
    const auto count=masks_.fullResolutionMasks.size()+masks.fullResolutionMasks.size();
    if(count>256 || pixels.value()>56*1024*1024/(count+1) || revision_==std::numeric_limits<Revision>::max())return error(ErrorCode::BudgetExceeded,"review.append-budget");
    for(const auto& mask:masks.fullResolutionMasks)if(mask.size()!=pixels.value())return error(ErrorCode::InvalidInput,"review.append-layout");
    auto nextProposals=proposals_;auto nextMasks=masks_;auto nextLocked=locked_;
    auto incoming=proposals.candidates;std::set<Id> ids;std::uint64_t ordinal=1;
    for(const auto& candidate:proposals_.candidates)ids.insert(candidate.id);
    for(auto& candidate:incoming){
        if(candidate.id.empty() || ids.contains(candidate.id)){while(ids.contains(Id::fromNumber(ordinal)))++ordinal;candidate.id=Id::fromNumber(ordinal++);}
        ids.insert(candidate.id);
    }
    nextProposals.candidates.insert(prepend?nextProposals.candidates.begin():nextProposals.candidates.end(),incoming.begin(),incoming.end());
    nextMasks.fullResolutionMasks.insert(prepend?nextMasks.fullResolutionMasks.begin():nextMasks.fullResolutionMasks.end(),masks.fullResolutionMasks.begin(),masks.fullResolutionMasks.end());
    nextLocked.insert(prepend?nextLocked.begin():nextLocked.end(),incoming.size(),false);recomputeUnassigned(nextMasks);
    undo_.push_back({proposals_,masks_,locked_});proposals_=std::move(nextProposals);masks_=std::move(nextMasks);locked_=std::move(nextLocked);++revision_;return {};
}
Result<void> ReviewSession::addBackground(std::span<const std::size_t> selected,std::span<const std::uint8_t> alpha,Revision expected){
    if(expected!=revision_)return error(ErrorCode::StaleRevision,"review.revision");
    const auto pixels=checkedMultiply(masks_.dimensions.width,masks_.dimensions.height);
    if(!pixels || selected.empty() || (!alpha.empty() && alpha.size()!=pixels.value()))return error(ErrorCode::InvalidInput,"review.background-source");
    if(pixels.value()>56*1024*1024/(masks_.fullResolutionMasks.size()+2))return error(ErrorCode::BudgetExceeded,"review.append-budget");
    for(const auto index:selected)if(index>=masks_.fullResolutionMasks.size())return error(ErrorCode::InvalidInput,"review.background-selection");
    const auto binary=[](auto value){return value==0 || value==255;};
    if(!std::all_of(alpha.begin(),alpha.end(),binary))return error(ErrorCode::UnsupportedFeature,"review.background-binary-alpha");
    Bytes background(std::size_t(pixels.value()),255);if(!alpha.empty())std::copy(alpha.begin(),alpha.end(),background.begin());
    for(const auto index:selected){
        const auto& foreground=masks_.fullResolutionMasks[index];
        if(!std::all_of(foreground.begin(),foreground.end(),binary))return error(ErrorCode::UnsupportedFeature,"review.background-binary-mask");
        for(std::size_t p=0;p<background.size();++p)if(foreground[p])background[p]=0;
    }
    if(std::none_of(background.begin(),background.end(),[](auto value){return value!=0;}))return error(ErrorCode::InvalidInput,"review.background-empty");
    ProposalSet p;p.sourceSize=masks_.dimensions;ProposalCandidate candidate;candidate.bounds={0,0,p.sourceSize.width,p.sourceSize.height};p.candidates.push_back(candidate);
    MaskSet m;m.dimensions=masks_.dimensions;m.fullResolutionMasks.push_back(std::move(background));
    return appendProposals(p,m,expected,true);
}
Result<void> ReviewSession::undo(){
    if(undo_.empty())
        return error(ErrorCode::InvalidInput,"review.undo-empty");
    proposals_=std::move(undo_.back().proposals);
    masks_=std::move(undo_.back().masks);
    locked_=std::move(undo_.back().locked);
    undo_.pop_back();
    revision_++;
    return{};
}

Result<ProposalSet> ReviewSession::refine(const PromptSet& prompts,std::stop_token stop)const{
    if(stop.stop_requested())
        return error(ErrorCode::Cancelled,"review.refine-cancelled");
    if(!refineRunner_)
        return error(ErrorCode::UnsupportedFeature,"review.refine-runner");
    for(const auto& point:prompts.points)
        if(!std::isfinite(point.x)||!std::isfinite(point.y))
            return error(ErrorCode::InvalidInput,"review.refine-point");
    for(const auto& box:prompts.boxes)
        if(!std::isfinite(box.x0)||!std::isfinite(box.y0)||
           !std::isfinite(box.x1)||!std::isfinite(box.y1)||
           !(box.x1>box.x0&&box.y1>box.y0))
            return error(ErrorCode::InvalidInput,"review.refine-box");
    auto result=refineRunner_(prompts,stop);
    if(!result)
        return result.error();
    if(result.value().sourceSize!=proposals_.sourceSize)
        return error(ErrorCode::CorruptData,"review.refine-size");
    for(const auto& candidate:result.value().candidates)
        if(candidate.lowResMask.size()!=256*256)
            return error(ErrorCode::CorruptData,"review.refine-mask");
    return result;
}

Result<ReviewedObjects> ReviewSession::accept(Revision expected)const{
    if(expected!=revision_)
        return error(ErrorCode::StaleRevision,"review.accept-revision");
    if(masks_.fullResolutionMasks.empty())
        return error(ErrorCode::InvalidInput,"review.empty");
    return ReviewedObjects{masks_.fullResolutionMasks,revision_,true};
}
}
