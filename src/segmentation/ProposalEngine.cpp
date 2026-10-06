#include "ProposalEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

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

Result<std::array<float,3>> sourcePixel(const AnalysisRequest& r,
                                        std::uint64_t x,std::uint64_t y) {
    if (r.view.rgbTiles.empty() || x>=r.sourceSize.width || y>=r.sourceSize.height)
        return error(ErrorCode::InvalidInput,"proposal.analysis-pixel");
    const auto tileW=r.view.rgbTiles.front().width;
    const auto tileH=r.view.rgbTiles.front().height;
    if (!tileW || !tileH)
        return error(ErrorCode::CorruptData,"proposal.analysis-tile-size");
    const auto cols=(r.sourceSize.width+tileW-1)/tileW;
    const auto rows=(r.sourceSize.height+tileH-1)/tileH;
    auto count=checkedMultiply(cols,rows);
    if (!count || count.value()!=r.view.rgbTiles.size())
        return error(ErrorCode::UnsupportedFeature,"proposal.analysis-tile-grid");
    const auto tx=x/tileW,ty=y/tileH;
    const auto& tile=r.view.rgbTiles[std::size_t(ty*cols+tx)];
    if (tile.format.mode!=ColorMode::RGB ||
        (tile.format.depth!=Depth::U8 && tile.format.depth!=Depth::U16))
        return error(ErrorCode::InvalidInput,"proposal.analysis-rgb");
    const auto lx=x-tx*tileW,ly=y-ty*tileH;
    if (lx>=tile.width || ly>=tile.height)
        return error(ErrorCode::CorruptData,"proposal.analysis-edge-tile");
    const unsigned channels=tile.format.channelCount();
    const unsigned sample=tile.format.depth==Depth::U16?2u:1u;
    const auto pixelBytes=std::uint64_t(channels)*sample;
    const auto offset=ly*tile.rowStride+lx*pixelBytes;
    if (offset+3*sample>tile.bytes.size())
        return error(ErrorCode::CorruptData,"proposal.analysis-layout");
    std::array<float,3> out{};
    for(unsigned c=0;c<3;c++){
        if(sample==1) out[c]=float(tile.bytes[std::size_t(offset+c)])/255.0f;
        else {
            const auto at=std::size_t(offset+c*2);
            const auto v=std::uint16_t(tile.bytes[at] |
                                      (std::uint16_t(tile.bytes[at+1])<<8));
            out[c]=float(v)/65535.0f;
        }
    }
    return out;
}

Result<Bytes> imageTensor(const AnalysisRequest& r) {
    constexpr std::uint64_t side=1024;
    auto pixels=checkedMultiply(side,side);
    if(!pixels) return pixels.error();
    auto floats=checkedMultiply(pixels.value(),3);
    if(!floats) return floats.error();
    auto bytes=checkedMultiply(floats.value(),sizeof(float));
    if(!bytes || bytes.value()>SIZE_MAX)
        return error(ErrorCode::BudgetExceeded,"proposal.image-tensor");
    std::vector<float> values(std::size_t(floats.value()));
    constexpr std::array<float,3> mean{0.485f,0.456f,0.406f};
    constexpr std::array<float,3> stdev{0.229f,0.224f,0.225f};
    for(std::uint64_t oy=0;oy<side;oy++){
        const double sy=(double(oy)+0.5)*double(r.sourceSize.height)/side-0.5;
        const auto y0=std::uint64_t(std::clamp<std::int64_t>(
            std::int64_t(std::floor(sy)),0,std::int64_t(r.sourceSize.height-1)));
        const auto y1=std::min<std::uint64_t>(y0+1,r.sourceSize.height-1);
        const float fy=float(std::clamp(sy-double(y0),0.0,1.0));
        for(std::uint64_t ox=0;ox<side;ox++){
            const double sx=(double(ox)+0.5)*double(r.sourceSize.width)/side-0.5;
            const auto x0=std::uint64_t(std::clamp<std::int64_t>(
                std::int64_t(std::floor(sx)),0,std::int64_t(r.sourceSize.width-1)));
            const auto x1=std::min<std::uint64_t>(x0+1,r.sourceSize.width-1);
            const float fx=float(std::clamp(sx-double(x0),0.0,1.0));
            auto p00=sourcePixel(r,x0,y0); if(!p00) return p00.error();
            auto p10=sourcePixel(r,x1,y0); if(!p10) return p10.error();
            auto p01=sourcePixel(r,x0,y1); if(!p01) return p01.error();
            auto p11=sourcePixel(r,x1,y1); if(!p11) return p11.error();
            for(unsigned ch=0;ch<3;ch++){
                const auto top=p00.value()[ch]*(1-fx)+p10.value()[ch]*fx;
                const auto bottom=p01.value()[ch]*(1-fx)+p11.value()[ch]*fx;
                const auto v=top*(1-fy)+bottom*fy;
                values[std::size_t(ch*side*side+oy*side+ox)]=(v-mean[ch])/stdev[ch];
            }
        }
    }
    Bytes out(std::size_t(bytes.value()));
    std::memcpy(out.data(),values.data(),out.size());
    return out;
}

template<class T>
Bytes pack(const std::vector<T>& values){
    Bytes out(values.size()*sizeof(T));
    if(!out.empty()) std::memcpy(out.data(),values.data(),out.size());
    return out;
}

Result<InferenceRequest> inferenceRequest(const AnalysisRequest& r) {
    auto image=imageTensor(r);
    if(!image) return image.error();
    std::vector<float> coords;
    std::vector<std::int64_t> labels;
    const auto sx=1024.0/double(r.sourceSize.width);
    const auto sy=1024.0/double(r.sourceSize.height);
    for(const auto& box:r.prompts.boxes){
        if(!(box.x1>box.x0 && box.y1>box.y0)) continue;
        coords.insert(coords.end(),{
            float(std::clamp(box.x0,0.0,double(r.sourceSize.width))*sx),
            float(std::clamp(box.y0,0.0,double(r.sourceSize.height))*sy),
            float(std::clamp(box.x1,0.0,double(r.sourceSize.width))*sx),
            float(std::clamp(box.y1,0.0,double(r.sourceSize.height))*sy)});
        labels.push_back(2);
        labels.push_back(3);
    }
    for(const auto& point:r.prompts.points){
        if(!std::isfinite(point.x)||!std::isfinite(point.y))
            return error(ErrorCode::InvalidInput,"proposal.prompt-nonfinite");
        coords.push_back(float(std::clamp(point.x,0.0,double(r.sourceSize.width))*sx));
        coords.push_back(float(std::clamp(point.y,0.0,double(r.sourceSize.height))*sy));
        labels.push_back(point.positive?1:0);
    }
    if(labels.empty())
        return error(ErrorCode::InvalidInput,"proposal.prompt-required");
    std::vector<float> mask(256*256,0.0f);
    std::vector<float> hasMask{0.0f};
    InferenceRequest request;
    std::copy_n(r.model.sha256.bytes.begin(),16,request.modelId.bytes.begin());
    if(request.modelId.empty()) request.modelId=Id::fromNumber(1);
    request.device=DevicePreference::CPU;
    request.tensorRefs={
        std::move(image.value()),
        pack(coords),
        pack(labels),
        pack(mask),
        pack(hasMask)};
    return request;
}

Result<ProposalSet> runtimeProposals(const AnalysisRequest& r,
                                     const InferenceRunner& runner,
                                     std::stop_token st) {
    auto request=inferenceRequest(r);
    if(!request) return request.error();
    auto inference=runner(r.model,request.value(),st);
    if(!inference) return inference.error();
    if(inference.value().tensorRefs.size()!=2)
        return error(ErrorCode::CorruptData,"proposal.output-count");
    const auto& maskBytes=inference.value().tensorRefs[0];
    const auto& scoreBytes=inference.value().tensorRefs[1];
    constexpr std::size_t plane=256*256;
    constexpr std::size_t planeBytes=plane*sizeof(float);
    if(maskBytes.empty() || maskBytes.size()%planeBytes)
        return error(ErrorCode::CorruptData,"proposal.mask-output-layout");
    const auto count=maskBytes.size()/planeBytes;
    if(count>64 || scoreBytes.size()<count*sizeof(float))
        return error(ErrorCode::CorruptData,"proposal.score-output-layout");

    ProposalSet out;
    out.modelHash=r.model.sha256;
    out.inputRevision=r.inputRevision;
    out.sourceSize=r.sourceSize;
    out.sourceToAnalysis.affine={
        1024.0/double(r.sourceSize.width),0,0,
        1024.0/double(r.sourceSize.height),0,0};
    out.sourceAlpha=r.view.sourceAlpha;
    for(std::size_t m=0;m<count;m++){
        ProposalCandidate candidate;
        candidate.id=Id::fromNumber(m+1);
        candidate.lowResMask.resize(plane);
        int minx=256,miny=256,maxx=-1,maxy=-1;
        for(int y=0;y<256;y++)for(int x=0;x<256;x++){
            float logit{};
            std::memcpy(&logit,maskBytes.data()+m*planeBytes+
                        std::size_t(y*256+x)*sizeof(float),sizeof(float));
            if(!std::isfinite(logit))
                return error(ErrorCode::CorruptData,"proposal.nonfinite-mask");
            if(logit>0){
                candidate.lowResMask[std::size_t(y*256+x)]=255;
                minx=std::min(minx,x);miny=std::min(miny,y);
                maxx=std::max(maxx,x);maxy=std::max(maxy,y);
            }
        }
        float score{};
        std::memcpy(&score,scoreBytes.data()+m*sizeof(float),sizeof(float));
        if(!std::isfinite(score))
            return error(ErrorCode::CorruptData,"proposal.nonfinite-score");
        candidate.score=score;
        if(maxx>=minx && maxy>=miny){
            candidate.bounds={
                std::int64_t(minx*4),std::int64_t(miny*4),
                std::uint64_t((maxx-minx+1)*4),
                std::uint64_t((maxy-miny+1)*4)};
        }
        out.candidates.push_back(std::move(candidate));
    }
    return out;
}

Result<ProposalSet> manualProposals(const AnalysisRequest& r) {
    ProposalSet out;
    out.modelHash=r.model.sha256;
    out.inputRevision=r.inputRevision;
    out.sourceSize=r.sourceSize;
    out.sourceToAnalysis=r.view.sourceTransform;
    out.sourceAlpha=r.view.sourceAlpha;
    std::uint64_t n=1;
    for(const auto& b:r.prompts.boxes){
        if(!(b.x1>b.x0&&b.y1>b.y0)) continue;
        const auto sx0=std::clamp(b.x0,0.0,double(r.sourceSize.width));
        const auto sy0=std::clamp(b.y0,0.0,double(r.sourceSize.height));
        const auto sx1=std::clamp(b.x1,0.0,double(r.sourceSize.width));
        const auto sy1=std::clamp(b.y1,0.0,double(r.sourceSize.height));
        if(!(sx1>sx0&&sy1>sy0)) continue;
        ProposalCandidate candidate;
        candidate.id=Id::fromNumber(n++);
        candidate.bounds=mapBox(r.view.sourceTransform,sx0,sy0,sx1,sy1);
        candidate.score=0.0;
        candidate.lowResMask.assign(256*256,255);
        out.candidates.push_back(std::move(candidate));
    }
    if(out.candidates.empty())
        return error(ErrorCode::UnsupportedFeature,"proposal.runtime-or-manual-box-required");
    return out;
}
}

ProposalEngine::ProposalEngine()
    : runner_([](const ModelManifest& manifest,const InferenceRequest& request,
                 std::stop_token stop){
        OrtSession session(manifest);
        return session.run(request,stop);
      }) {}

ProposalEngine::ProposalEngine(InferenceRunner runner)
    : runner_(std::move(runner)), forceRuntime_(true) {}

Result<ProposalSet> ProposalEngine::propose(const AnalysisRequest& r,std::stop_token st)const {
    if(st.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
    if(!r.sourceSize.width||!r.sourceSize.height)
        return error(ErrorCode::InvalidInput,"proposal.size");
    auto tx=validateTransform(r.view.sourceTransform);
    if(!tx) return tx.error();
    AdapterRegistry ar;
    auto adapter=ar.resolve(r.model.taskId);
    if(!adapter) return adapter.error();
    if((forceRuntime_||!r.model.resolvedWeights.empty())&&!r.view.rgbTiles.empty())
        return runtimeProposals(r,runner_,st);
    return manualProposals(r);
}

Result<MaskSet> projectMasks(const ProposalSet& p,const ProjectionPolicy& policy) {
    if(!policy.targetSize.width||!policy.targetSize.height||policy.targetSize!=p.sourceSize)
        return error(ErrorCode::InvalidInput,"mask.target-size");
    auto tx=validateTransform(p.sourceToAnalysis);
    if(!tx) return tx.error();
    auto pixels=checkedMultiply(policy.targetSize.width,policy.targetSize.height);
    if(!pixels) return pixels.error();
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
                if(alpha) value=std::min(value,(*alpha)[idx]);
                mask[idx]=value;
                if(value) out.unassigned[idx]=0;
            }
        }
        out.fullResolutionMasks.push_back(std::move(mask));
    }
    return out;
}
}
