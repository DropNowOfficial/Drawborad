#include "CriticalRegions.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace drawborad {
namespace {
Result<void> validateSource(const CriticalRegionRequest& request) {
    if (!request.source || request.layerId.empty() || !request.reconstructed.nodes ||
        !request.reconstructed.eventIndex)
        return error(ErrorCode::InvalidInput,"critical.source-or-snapshot");
    const auto& snapshot=request.reconstructed;
    if (snapshot.revision!=request.expectedRevision)
        return error(ErrorCode::StaleRevision,"critical.stale-revision");
    const auto node=snapshot.nodes->find(request.layerId);
    if (node==snapshot.nodes->end() || node->second.kind!=NodeKind::Layer)
        return error(ErrorCode::InvalidInput,"critical.layer");
    const auto& image=*request.source;
    const auto size=snapshot.spec.pixelSize;
    if (!size.width || !size.height || size.width>INT32_MAX || size.height>INT32_MAX ||
        image.width!=size.width || image.height!=size.height ||
        image.format!=snapshot.spec.colorSpec.format)
        return error(ErrorCode::InvalidInput,"critical.source-layout");
    if (image.profileId!=snapshot.spec.colorSpec.profileId)
        return error(ErrorCode::InvalidProfile,"critical.profile-mismatch");
    const auto bytesPer=image.format.depth==Depth::U16?2u:1u;
    const auto row=checkedMultiply(size.width,
                      std::uint64_t(image.format.channelCount())*bytesPer);
    if (!row)return row.error();
    const auto total=checkedMultiply(image.rowStride,size.height);
    if (!total)return total.error();
    if (image.rowStride<row.value() || total.value()>image.bytes.size())
        return error(ErrorCode::InvalidInput,"critical.source-stride");
    if (request.regions.empty() || request.regions.size()>128)
        return error(ErrorCode::InvalidInput,"critical.empty-or-many-regions");
    for (const auto& region:request.regions) {
        const auto b=region.bounds;
        if (b.x<0 || b.y<0 || !b.width || !b.height ||
            std::uint64_t(b.x)>=size.width || std::uint64_t(b.y)>=size.height ||
            b.width>size.width-std::uint64_t(b.x) ||
            b.height>size.height-std::uint64_t(b.y) ||
            b.width>1048576 || b.height>1048576 ||
            !std::isfinite(region.maxColorMae) || !std::isfinite(region.maxAlphaMae) ||
            !std::isfinite(region.maxEdgeMismatch) ||
            region.maxColorMae<0 || region.maxColorMae>1 ||
            region.maxAlphaMae<0 || region.maxAlphaMae>1 ||
            region.maxEdgeMismatch<0 || region.maxEdgeMismatch>1)
            return error(ErrorCode::InvalidInput,"critical.region");
        auto area=checkedMultiply(b.width,b.height);
        if (!area || area.value()>32ull*1024*1024)
            return error(ErrorCode::BudgetExceeded,"critical.region-budget");
    }
    return {};
}
inline double sample(const TileData& tile,std::uint64_t x,std::uint64_t y,unsigned ch) {
    const auto bytes=tile.format.depth==Depth::U16?2u:1u;
    const auto offset=std::size_t(y*tile.rowStride+(x*tile.format.channelCount()+ch)*bytes);
    if(bytes==1)return double(tile.bytes[offset])/255.0;
    return double(std::uint16_t(tile.bytes[offset]) |
                  (std::uint16_t(tile.bytes[offset+1])<<8))/65535.0;
}
inline double premulInk(const TileData& tile,std::uint64_t x,std::uint64_t y) {
    const auto alphaChannel=tile.format.channelCount()-1;
    const double alpha=sample(tile,x,y,alphaChannel);
    // Native channel-coverage proxy, not perceptual L*a*b* / Delta E00.
    const unsigned colorChannels=tile.format.mode==ColorMode::CMYK?4u:3u;
    double sum=0;
    for(unsigned ch=0;ch<colorChannels;++ch)sum+=sample(tile,x,y,ch);
    return alpha*(sum/colorChannels);
}
inline bool hasEdge(const TileData& tile,std::uint64_t x,std::uint64_t y,
                    bool right,bool down) {
    const auto center=premulInk(tile,x,y);
    if (right && std::abs(center-premulInk(tile,x+1,y))>0.12)return true;
    if (down && std::abs(center-premulInk(tile,x,y+1))>0.12)return true;
    return false;
}
}
Result<CriticalRegionReport> checkCriticalRegions(const CriticalRegionRequest& request,
                                                  std::stop_token stop) {
    if (stop.stop_requested())return error(ErrorCode::Cancelled,"critical.cancelled");
    auto valid=validateSource(request);
    if(!valid)return valid.error();
    const auto& source=*request.source;
    ReferenceRenderer renderer;
    CriticalRegionReport report;
    report.accepted=true;
    for(const auto& roi:request.regions) {
        if(stop.stop_requested())return error(ErrorCode::Cancelled,"critical.cancelled");
        const auto b=roi.bounds;
        CriticalRegionMetric score;
        score.kind=roi.kind;
        score.label=roi.label;
        score.bounds=b;
        double colorSum=0,alphaSum=0,weightSum=0;
        std::uint64_t mismatch=0,edgeUnion=0;
        const std::uint64_t xBegin=std::uint64_t(b.x),yBegin=std::uint64_t(b.y);
        const auto xEnd=xBegin+b.width,yEnd=yBegin+b.height;
        // A one-pixel lookahead ensures edges are checked across 512x512 render tiles.
        for(auto top=yBegin;top<yEnd;top+=511)for(auto left=xBegin;left<xEnd;left+=511){
            if(stop.stop_requested())return error(ErrorCode::Cancelled,"critical.cancelled");
            const auto cw=std::min<std::uint64_t>(511,xEnd-left);
            const auto ch=std::min<std::uint64_t>(511,yEnd-top);
            const Rect tile{std::int64_t(left),std::int64_t(top),
                cw+(left+cw<xEnd?1u:0u),ch+(top+ch<yEnd?1u:0u)};
            const EventLimit limit{request.reconstructed.revision,
                request.reconstructed.eventIndex->size(),UINT64_MAX};
            auto rendered=renderer.renderRegion(request.reconstructed,
                                                  request.layerId,tile,limit,stop);
            if(!rendered)return rendered.error();
            const auto& painted=rendered.value();
            const unsigned colors=source.format.mode==ColorMode::CMYK?4u:3u;
            for(std::uint64_t y=0;y<ch;++y){
                if((y&63)==0 && stop.stop_requested())
                    return error(ErrorCode::Cancelled,"critical.cancelled");
                for(std::uint64_t x=0;x<cw;++x){
                    const auto sx=left+x,sy=top+y;
                    const auto targetAlpha=sample(source,sx,sy,colors);
                    const auto paintedAlpha=sample(painted,x,y,colors);
                    alphaSum+=std::abs(targetAlpha-paintedAlpha);
                    if(targetAlpha>1.0/65535.0){
                        weightSum+=targetAlpha;
                        for(unsigned k=0;k<colors;++k)
                            colorSum+=std::abs(sample(source,sx,sy,k)-
                                               sample(painted,x,y,k))*targetAlpha/colors;
                    }
                    const bool right=sx+1<xEnd,down=sy+1<yEnd;
                    const auto refEdge=hasEdge(source,sx,sy,right,down);
                    const auto paintedEdge=hasEdge(painted,x,y,right,down);
                    if(refEdge || paintedEdge){
                        ++edgeUnion;
                        if(refEdge!=paintedEdge)++mismatch;
                    }
                    ++score.comparedPixels;
                }
            }
        }
        score.alphaMae=alphaSum/score.comparedPixels;
        score.colorMae=weightSum>0?colorSum/weightSum:0.0;
        score.edgeMismatch=edgeUnion?double(mismatch)/double(edgeUnion):0.0;
        score.passed=score.alphaMae<=roi.maxAlphaMae &&
                     score.colorMae<=roi.maxColorMae &&
                     score.edgeMismatch<=roi.maxEdgeMismatch;
        if(!score.passed)report.accepted=false;
        report.regions.push_back(std::move(score));
    }
    return report;
}
} // namespace drawborad
