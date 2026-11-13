#include "QtSolidDraftPreview.h"
#include "render/CoverageRasterizer.h"
#include <algorithm>
#include <cmath>
#include <new>
namespace drawborad {
namespace {
bool unit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
Error maskError(QtSolidMaskCache::Status status) {
    using S=QtSolidMaskCache::Status;
    if(status==S::StaleGeneration) return error(ErrorCode::StaleRevision,"solid-preview.generation");
    if(status==S::Cancelled) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
    if(status==S::UnsupportedBackend) return error(ErrorCode::UnsupportedFeature,"solid-preview.backend");
    if(status==S::BudgetExceeded || status==S::AllocationFailed) return error(ErrorCode::BudgetExceeded,"solid-preview.mask-budget");
    return error(ErrorCode::InvalidInput,"solid-preview.mask-region-or-id");
}
}
Result<TileData> renderQtSolidDraftPreview(const QtSolidMaskCache& cache,
    std::uint64_t generation, Rect region, std::span<const QtSolidDraftPaint> paints,
    const TileData& background, std::stop_token stop) {
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
    if(!generation || generation!=cache.generation()) return error(ErrorCode::StaleRevision,"solid-preview.generation");
    if(paints.empty()) return error(ErrorCode::InvalidInput,"solid-preview.empty-paints");
    if(paints.size()>8) return error(ErrorCode::BudgetExceeded,"solid-preview.paint-count");
    if(background.format.mode!=ColorMode::RGB || !background.format.alpha ||
       (background.format.depth!=Depth::U8 && background.format.depth!=Depth::U16))
        return error(ErrorCode::UnsupportedFeature,"solid-preview.rgb-alpha-required");
    if(!region.width || !region.height || region.width>512 || region.height>512 ||
       region.x<0 || region.y<0 || region.x>8192 || region.y>8192 ||
       region.width>8192-static_cast<std::uint64_t>(region.x) ||
       region.height>8192-static_cast<std::uint64_t>(region.y) ||
       background.width!=region.width || background.height!=region.height)
        return error(ErrorCode::InvalidInput,"solid-preview.region");
    const std::uint64_t sample=background.format.depth==Depth::U16?2:1;
    // Permit padding but cap ALL bytes copied, including padding, before allocation.
    constexpr std::uint64_t maxTileBytes=4*1024*1024;
    if(background.rowStride<region.width*4*sample ||
       background.rowStride>maxTileBytes/region.height ||
       background.bytes.size()!=background.rowStride*region.height)
        return error(ErrorCode::InvalidInput,"solid-preview.layout");
    for(std::size_t i=0;i<paints.size();++i) {
        const auto& paint=paints[i];
        if(!paint.maskId || paint.color.activeChannels!=3 ||
           paint.color.profileId!=background.profileId || !unit(paint.opacity) || !unit(paint.color.alpha))
            return error(ErrorCode::InvalidInput,"solid-preview.color");
        for(double channel:paint.color.channels)
            if(!unit(channel)) return error(ErrorCode::InvalidInput,"solid-preview.color");
        for(std::size_t j=0;j<i;++j)
            if(paints[j].maskId==paint.maskId) return error(ErrorCode::InvalidInput,"solid-preview.duplicate-mask");
    }
    try {
        // Private result: subsequent bad IDs/regions/cancellation cannot mutate input.
        TileData output=background;
        std::vector<std::uint16_t> coverage(region.width*region.height);
        std::vector<std::uint8_t> chunk(256*256);
        for(const auto& paint:paints) {
            if(stop.stop_requested()) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
            for(std::uint64_t y=0;y<region.height;y+=256)
                for(std::uint64_t x=0;x<region.width;x+=256) {
                    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
                    const auto w=std::min<std::uint64_t>(256,region.width-x);
                    const auto h=std::min<std::uint64_t>(256,region.height-y);
                    const auto status=cache.readTile(generation,paint.maskId,
                        QRect(int(region.x+x),int(region.y+y),int(w),int(h)),chunk,w);
                    if(status!=QtSolidMaskCache::Status::Ok) return maskError(status);
                    for(std::size_t row=0;row<h;++row)
                        for(std::size_t col=0;col<w;++col)
                            coverage[(y+row)*region.width+x+col]=std::uint16_t(chunk[row*w+col])*257u;
                }
            if(stop.stop_requested()) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
            // Local paint carrier only: never inserted in Document or serialized.
            StrokeEvent carrier; carrier.toolId="paint"; carrier.color=paint.color;
            carrier.style.opacity=paint.opacity;
            blendCoverage(output,carrier,coverage);
        }
        if(stop.stop_requested()) return error(ErrorCode::Cancelled,"solid-preview.cancelled");
        return output;
    } catch(const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded,"solid-preview.allocation");
    }
}
}
