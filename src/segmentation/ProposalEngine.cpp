#include "ProposalEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace drawborad {
struct ProposalEngineState {
    std::mutex mutex;
    PixelSize size{};
    std::vector<TileData> tiles;
    Bytes image;
    std::optional<ModelManifest> model;
    std::optional<OrtSession> session;
};
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

Result<InferenceRequest> inferenceRequest(const AnalysisRequest& r, const Bytes& image) {
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
        image,
        pack(coords),
        pack(labels),
        pack(mask),
        pack(hasMask)};
    return request;
}

Result<ProposalSet> runtimeProposals(const AnalysisRequest& r,
                                     const InferenceRunner& runner,
                                     std::stop_token st, const Bytes& image) {
    auto request=inferenceRequest(r,image);
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
        candidate.lowResLogits.resize(plane);
        int minx=256,miny=256,maxx=-1,maxy=-1;
        for(int y=0;y<256;y++)for(int x=0;x<256;x++){
            float logit{};
            std::memcpy(&logit,maskBytes.data()+m*planeBytes+
                        std::size_t(y*256+x)*sizeof(float),sizeof(float));
            if(!std::isfinite(logit))
                return error(ErrorCode::CorruptData,"proposal.nonfinite-mask");
            candidate.lowResLogits[std::size_t(y*256+x)]=std::clamp(logit,-32.0f,32.0f);
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

ProposalEngine::ProposalEngine() : state_(std::make_shared<ProposalEngineState>()) {}

ProposalEngine::ProposalEngine(InferenceRunner runner)
    : runner_(std::move(runner)), forceRuntime_(true), state_(std::make_shared<ProposalEngineState>()) {}

Result<ProposalSet> ProposalEngine::propose(const AnalysisRequest& r,std::stop_token st)const {
    if(st.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
    if(!r.sourceSize.width||!r.sourceSize.height)
        return error(ErrorCode::InvalidInput,"proposal.size");
    auto tx=validateTransform(r.view.sourceTransform);
    if(!tx) return tx.error();
    AdapterRegistry ar;
    auto adapter=ar.resolve(r.model.taskId);
    if(!adapter) return adapter.error();
    if(forceRuntime_||!r.model.resolvedWeights.empty()) {
        if(r.view.rgbTiles.empty())
            return error(ErrorCode::InvalidInput,"proposal.analysis-image-required");
        std::lock_guard lock(state_->mutex);
        if(state_->image.empty() || state_->size!=r.sourceSize || state_->tiles!=r.view.rgbTiles){
            auto image=imageTensor(r);
            if(!image) return image.error();
            auto tiles=r.view.rgbTiles;
            state_->tiles=std::move(tiles);
            state_->size=r.sourceSize;
            state_->image=std::move(image.value());
        }
        if(runner_) return runtimeProposals(r,runner_,st,state_->image);
        if(!state_->model || *state_->model!=r.model){
            state_->session.emplace(r.model);
            state_->model=r.model;
        }
        return runtimeProposals(r,[&](const ModelManifest&,const InferenceRequest& request,
                                     std::stop_token stop){return state_->session->run(request,stop);},
                                st,state_->image);
    }
    return manualProposals(r);
}

Result<MaskSet> projectMasks(const ProposalSet& p,const ProjectionPolicy& policy) {
    if(!std::isfinite(policy.threshold))
        return error(ErrorCode::InvalidInput,"mask.threshold");
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
        if(!candidate.lowResLogits.empty() &&
           (candidate.lowResLogits.size()!=256*256 ||
            !std::all_of(candidate.lowResLogits.begin(),candidate.lowResLogits.end(),
                         [](float value){return std::isfinite(value);})))
            return error(ErrorCode::CorruptData,"mask.lowres-logits");
        if(!candidate.lowResLogits.empty()){
            Bytes mask(std::size_t(pixels.value()),0);
            for(std::uint64_t y=0;y<policy.targetSize.height;y++){
                for(std::uint64_t x=0;x<policy.targetSize.width;x++){
                    const auto analysis=mapPoint(p.sourceToAnalysis,double(x)+0.5,double(y)+0.5);
                    if(analysis.first<0 || analysis.first>=1024 ||
                       analysis.second<0 || analysis.second>=1024) continue;
                    // Same half-pixel, align_corners=false bilinear sampling as SAM.
                    const auto sx=std::clamp(analysis.first/4.0-0.5,0.0,255.0);
                    const auto sy=std::clamp(analysis.second/4.0-0.5,0.0,255.0);
                    const auto x0=std::size_t(std::floor(sx)), y0=std::size_t(std::floor(sy));
                    const auto x1=std::min(x0+1,std::size_t(255));
                    const auto y1=std::min(y0+1,std::size_t(255));
                    const auto fx=float(sx-double(x0)), fy=float(sy-double(y0));
                    const auto& logits=candidate.lowResLogits;
                    const auto top=logits[y0*256+x0]*(1-fx)+logits[y0*256+x1]*fx;
                    const auto bottom=logits[y1*256+x0]*(1-fx)+logits[y1*256+x1]*fx;
                    auto value=std::uint8_t(top*(1-fy)+bottom*fy>policy.threshold?255:0);
                    const auto idx=std::size_t(y*policy.targetSize.width+x);
                    if(alpha) value=std::min(value,(*alpha)[idx]);
                    mask[idx]=value;
                    if(value) out.unassigned[idx]=0;
                }
            }
            out.fullResolutionMasks.push_back(std::move(mask));
            continue;
        }
        if(!candidate.bounds.width||!candidate.bounds.height){
            out.fullResolutionMasks.emplace_back(std::size_t(pixels.value()),std::uint8_t{0});
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

Result<ProposalSet> ProposalEngine::proposeAutomatic(const AnalysisRequest& input,
    const AutomaticProposalOptions& options, std::stop_token stop, AnalysisProgress progress) const {
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
    if(!input.prompts.points.empty() || !input.prompts.boxes.empty())
        return error(ErrorCode::InvalidInput,"proposal.automatic-prompts");
    if(!options.pointsPerSide || options.pointsPerSide>64 ||
       !options.maxCandidates || options.maxCandidates>256)
        return error(ErrorCode::InvalidInput,"proposal.automatic-grid");
    for(const auto threshold:{options.predictedIouThreshold,options.stabilityThreshold,options.boxNmsThreshold})
        if(!std::isfinite(threshold) || threshold<0 || threshold>1)
            return error(ErrorCode::InvalidInput,"proposal.automatic-threshold");
    if(!forceRuntime_ && input.model.resolvedWeights.empty())
        return error(ErrorCode::UnsupportedFeature,"proposal.automatic-runtime-required");

    struct Candidate { unsigned point{}; std::size_t mask{}; double score{}; Rect box{}; };
    std::vector<Candidate> candidates;
    const auto side=options.pointsPerSide, total=side*side;
    AnalysisRequest request=input;
    auto setPoint=[&](unsigned point){
        request.prompts.points={{(double(point%side)+.5)*double(input.sourceSize.width)/side,
                                (double(point/side)+.5)*double(input.sourceSize.height)/side,true}};
    };
    ProposalSet result;
    for(unsigned point=0;point<total;++point){
        if(stop.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
        setPoint(point);
        auto proposed=propose(request,stop);
        if(!proposed) return proposed.error();
        if(point==0){result=proposed.value(); result.candidates.clear();}
        for(std::size_t mask=0;mask<proposed.value().candidates.size();++mask){
            const auto& candidate=proposed.value().candidates[mask];
            if(candidate.score<=options.predictedIouThreshold) continue;
            ProposalSet single=proposed.value(); single.candidates={candidate};
            auto binary=projectMasks(single,{input.sourceSize,false,0});
            auto high=projectMasks(single,{input.sourceSize,false,1});
            auto low=projectMasks(single,{input.sourceSize,false,-1});
            if(!binary) return binary.error();
            if(!high) return high.error();
            if(!low) return low.error();
            const auto& bytes=binary.value().fullResolutionMasks[0];
            std::uint64_t above=0, below=0;
            std::int64_t minx=std::int64_t(input.sourceSize.width),miny=std::int64_t(input.sourceSize.height);
            std::int64_t maxx=-1,maxy=-1;
            for(std::size_t p=0;p<bytes.size();++p){
                above+=high.value().fullResolutionMasks[0][p]>0;
                below+=low.value().fullResolutionMasks[0][p]>0;
                if(bytes[p]){
                    const auto x=std::int64_t(p%input.sourceSize.width),y=std::int64_t(p/input.sourceSize.width);
                    minx=std::min(minx,x);miny=std::min(miny,y);maxx=std::max(maxx,x);maxy=std::max(maxy,y);
                }
            }
            if(maxx<0 || !below || double(above)/double(below)<options.stabilityThreshold) continue;
            candidates.push_back({point,mask,candidate.score,{minx,miny,std::uint64_t(maxx-minx),std::uint64_t(maxy-miny)}});
        }
        if(progress) progress(point+1,total);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    const auto overlaps=[](const Rect& a,const Rect& b){
        if(a==b) return 1.0;
        const auto width=std::max(0.0,std::min(double(a.x)+double(a.width),double(b.x)+double(b.width))-double(std::max(a.x,b.x)));
        const auto height=std::max(0.0,std::min(double(a.y)+double(a.height),double(b.y)+double(b.height))-double(std::max(a.y,b.y)));
        const auto intersection=width*height;
        const auto joined=double(a.width)*double(a.height)+double(b.width)*double(b.height)-intersection;
        return joined>0?intersection/joined:0.0;
    };
    std::vector<Candidate> retained;
    for(const auto& candidate:candidates){
        if(std::any_of(retained.begin(),retained.end(),[&](const auto& other){return overlaps(candidate.box,other.box)>options.boxNmsThreshold;})) continue;
        if(retained.size()==options.maxCandidates)
            return error(ErrorCode::BudgetExceeded,"proposal.automatic-candidate-limit");
        retained.push_back(candidate);
    }
    // Keep only small metadata during sampling. Re-run retained prompts using
    // the same cached image features instead of retaining thousands of planes.
    for(const auto& candidate:retained){
        if(stop.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
        setPoint(candidate.point);
        auto proposed=propose(request,stop);
        if(!proposed) return proposed.error();
        if(candidate.mask>=proposed.value().candidates.size())
            return error(ErrorCode::CorruptData,"proposal.automatic-recovery");
        auto selected=std::move(proposed.value().candidates[candidate.mask]);
        selected.id=Id::fromNumber(std::uint64_t(candidate.point)*64+candidate.mask+1);
        result.candidates.push_back(std::move(selected));
    }
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"proposal.cancelled");
    return result;
}
}
