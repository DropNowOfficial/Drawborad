#include "ProposalEngine.h"
#include <algorithm>
#include <cmath>

namespace drawborad {
namespace {
Result<void> validateTransform(const LayerTransform& t) {
    for (auto v : t.affine)
        if (!std::isfinite(v))
            return error(ErrorCode::InvalidInput, "proposal.transform-nonfinite");
    const auto det=t.affine[0]*t.affine[3]-t.affine[1]*t.affine[2];
    if (!std::isfinite(det) || std::abs(det)<1e-12)
        return error(ErrorCode::InvalidInput, "proposal.transform-singular");
    return {};
}
std::pair<double,double> mapPoint(const LayerTransform& t,double x,double y) {
    return {t.affine[0]*x+t.affine[2]*y+t.affine[4],
            t.affine[1]*x+t.affine[3]*y+t.affine[5]};
}
Rect mapBox(const LayerTransform& t,double x0,double y0,double x1,double y1) {
    auto p0=mapPoint(t,x0,y0);
    auto p1=mapPoint(t,x1,y0);
    auto p2=mapPoint(t,x0,y1);
    auto p3=mapPoint(t,x1,y1);
    const auto minx=std::min({p0.first,p1.first,p2.first,p3.first});
    const auto miny=std::min({p0.second,p1.second,p2.second,p3.second});
    const auto maxx=std::max({p0.first,p1.first,p2.first,p3.first});
    const auto maxy=std::max({p0.second,p1.second,p2.second,p3.second});
    const auto ix=std::int64_t(std::floor(minx));
    const auto iy=std::int64_t(std::floor(miny));
    return {ix,iy,std::uint64_t(std::max(0.0,std::ceil(maxx)-double(ix))),
                  std::uint64_t(std::max(0.0,std::ceil(maxy)-double(iy)))};
}
}

Result<ProposalSet> ProposalEngine::propose(const AnalysisRequest& r,std::stop_token st)const {
    if(st.stop_requested())
        return error(ErrorCode::Cancelled,"proposal.cancelled");
    if(!r.sourceSize.width||!r.sourceSize.height)
        return error(ErrorCode::InvalidInput,"proposal.size");
    auto tx=validateTransform(r.view.sourceTransform);
    if(!tx)
        return tx.error();
    AdapterRegistry ar;
    auto adapter=ar.resolve(r.model.taskId);
    if(!adapter)
        return adapter.error();

    ProposalSet out;
    out.modelHash=r.model.sha256;
    out.inputRevision=r.inputRevision;
    out.sourceSize=r.sourceSize;
    out.sourceToAnalysis=r.view.sourceTransform;
    out.sourceAlpha=r.view.sourceAlpha;

    std::uint64_t n=1;
    for(const auto& b:r.prompts.boxes){
        if(!(b.x1>b.x0&&b.y1>b.y0))
            continue;
        const auto sx0=std::clamp(b.x0,0.0,double(r.sourceSize.width));
        const auto sy0=std::clamp(b.y0,0.0,double(r.sourceSize.height));
        const auto sx1=std::clamp(b.x1,0.0,double(r.sourceSize.width));
        const auto sy1=std::clamp(b.y1,0.0,double(r.sourceSize.height));
        if(!(sx1>sx0&&sy1>sy0))
            continue;
        ProposalCandidate candidate;
        candidate.id=Id::fromNumber(n++);
        candidate.bounds=mapBox(r.view.sourceTransform,sx0,sy0,sx1,sy1);
        candidate.score=0.0; // manual prompt geometry has no model confidence
        candidate.lowResMask.assign(256*256,255);
        out.candidates.push_back(std::move(candidate));
    }

    if(out.candidates.empty()){
        ProposalCandidate candidate;
        candidate.id=Id::fromNumber(1);
        candidate.bounds=mapBox(r.view.sourceTransform,0,0,
                                double(r.sourceSize.width),double(r.sourceSize.height));
        candidate.score=0.0;
        candidate.lowResMask.assign(256*256,0);
        out.candidates.push_back(std::move(candidate));
    }
    return out;
}

Result<MaskSet> projectMasks(const ProposalSet& p,const ProjectionPolicy& policy) {
    if(!policy.targetSize.width||!policy.targetSize.height||policy.targetSize!=p.sourceSize)
        return error(ErrorCode::InvalidInput,"mask.target-size");
    auto tx=validateTransform(p.sourceToAnalysis);
    if(!tx)
        return tx.error();

    auto pixels=checkedMultiply(policy.targetSize.width,policy.targetSize.height);
    if(!pixels)
        return pixels.error();
    if(pixels.value()>512ull*1024*1024)
        return error(ErrorCode::BudgetExceeded,"mask.size");

    const Bytes* alpha=nullptr;
    if(policy.preserveAlpha&&!p.sourceAlpha.empty()){
        if(p.sourceAlpha.size()!=1||p.sourceAlpha.front().size()!=pixels.value())
            return error(ErrorCode::UnsupportedFeature,"mask.alpha-layout");
        alpha=&p.sourceAlpha.front();
    }

    MaskSet out;
    out.dimensions=policy.targetSize;
    out.unassigned.assign(std::size_t(pixels.value()),255);
    out.fullResolutionMasks.reserve(p.candidates.size());

    for(const auto& candidate:p.candidates){
        if(candidate.lowResMask.size()!=256*256)
            return error(ErrorCode::CorruptData,"mask.lowres-layout");
        if(!candidate.bounds.width||!candidate.bounds.height){
            out.fullResolutionMasks.emplace_back(std::size_t(pixels.value()),0);
            continue;
        }
        Bytes mask(std::size_t(pixels.value()),0);
        const double bx=double(candidate.bounds.x);
        const double by=double(candidate.bounds.y);
        const double bw=double(candidate.bounds.width);
        const double bh=double(candidate.bounds.height);

        for(std::uint64_t y=0;y<policy.targetSize.height;y++){
            for(std::uint64_t x=0;x<policy.targetSize.width;x++){
                auto analysis=mapPoint(p.sourceToAnalysis,double(x)+0.5,double(y)+0.5);
                if(analysis.first<bx||analysis.second<by||
                   analysis.first>=bx+bw||analysis.second>=by+bh)
                    continue;
                auto u=(analysis.first-bx)/bw;
                auto v=(analysis.second-by)/bh;
                auto mx=std::clamp<int>(int(std::floor(u*256.0)),0,255);
                auto my=std::clamp<int>(int(std::floor(v*256.0)),0,255);
                auto value=candidate.lowResMask[std::size_t(my*256+mx)];
                const auto idx=std::size_t(y*policy.targetSize.width+x);
                if(alpha)
                    value=std::min(value,(*alpha)[idx]);
                mask[idx]=value;
                if(value)
                    out.unassigned[idx]=0;
            }
        }
        out.fullResolutionMasks.push_back(std::move(mask));
    }
    return out;
}
}
