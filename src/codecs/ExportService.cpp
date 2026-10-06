#include "ExportService.h"
#include "storage/FileIO.h"
#include "codecs/AtomicExport.h"
#include "codecs/RasterWriters.h"
#include "codecs/LayerArchive.h"
#include <atomic>
#include <chrono>
namespace drawborad {
Result<ExportPlan> preflightExport(ColorMode mode,const ExportRequest&r){
    if(r.path.empty()||!r.size.width||!r.size.height)
        return error(ErrorCode::InvalidInput,"export.request");
    ExportPlan p{r,false,{}};
    if(mode==ColorMode::CMYK&&(r.format==ExportFormat::Png||r.format==ExportFormat::Webp||r.format==ExportFormat::Ora)){
        if(!r.conversionConsent)
            return error(ErrorCode::ConversionConsentRequired,"export.cmyk-rgb-consent");
        if(r.profile.empty())
            return error(ErrorCode::ConversionConsentRequired,"export.cmyk-rgb-profile");
        p.requiresRgbConversion=true;
        p.conversionList.push_back("CMYK -> RGB");
    }
    if(r.format==ExportFormat::Jpeg&&r.alpha&&!r.matteChosen)
        return error(ErrorCode::ConversionConsentRequired,"export.jpeg-matte");
    return p;
}
Result<ExportReport> writeRasterPlaceholder(const ExportPlan&p,std::span<const std::uint8_t> payload,std::stop_token st){
    if(st.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    if(p.request.path.empty())return error(ErrorCode::InvalidInput,"export.path");
    static std::atomic<std::uint64_t> serial{};
    auto stagedPath=p.request.path;
    stagedPath += std::string(".stage-") +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(serial++);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}
    } cleanup{stagedPath};
    NativeFileIO io;
    auto written=io.write(stagedPath,payload,true);
    if(!written)return written.error();
    if(st.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    StagedExport staged{stagedPath,sha256(payload)};
    DestinationPolicy policy{p.request.path,p.request.overwrite,true};
    auto committed=commitExport(io,staged,policy,2*GiB);
    if(!committed)return committed.error();
    ExportReport r;
    r.paths.push_back(p.request.path);
    r.hashes.push_back(staged.verifiedHash);
    r.dimensions=p.request.size;
    r.channels=p.request.alpha?4:3;
    if(p.requiresRgbConversion)r.warnings.push_back("explicit-rgb-conversion");
    return r;
}

Result<ExportReport> ExportService::write(const ExportPlan& plan,const TileData& merged,
                                          std::span<const PsdLayerPixels> layers,
                                          const RasterEncodingOptions& options,
                                          std::stop_token stop) const {
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"export.cancelled");
    if(merged.width!=plan.request.size.width||merged.height!=plan.request.size.height)
        return error(ErrorCode::InvalidInput,"export.size-mismatch");
    if(plan.requiresRgbConversion&&merged.format.mode!=ColorMode::RGB)
        return error(ErrorCode::ConversionConsentRequired,"export.rgb-conversion-not-applied");

    if(plan.request.format==ExportFormat::Png||plan.request.format==ExportFormat::Tiff||
       plan.request.format==ExportFormat::Jpeg||plan.request.format==ExportFormat::Webp)
        return writeRaster(plan,merged,options,stop);

    if(plan.request.format==ExportFormat::Psd){
        PsdWriteRequest q;
        q.path=plan.request.path;
        q.size=plan.request.size;
        q.format=merged.format;
        q.profile=plan.request.profile;
        q.iccProfile=options.iccProfile;
        q.ppi=options.ppi;
        q.layers.assign(layers.begin(),layers.end());
        q.merged=merged;
        q.overwrite=plan.request.overwrite;
        auto written=writePsd(q,stop);
        if(!written)return written.error();
        NativeFileIO io;
        auto bytes=io.read(q.path,2*GiB);
        if(!bytes)return bytes.error();
        ExportReport report;
        report.paths.push_back(q.path);
        report.hashes.push_back(sha256(bytes.value()));
        report.dimensions=q.size;
        report.channels=merged.format.channelCount();
        return report;
    }

    std::vector<ArchiveLayer> archive;
    archive.reserve(layers.size());
    for(const auto& layer:layers){
        ArchiveLayer a;
        a.name=layer.name;
        a.pixels=layer.pixels;
        a.opacity=double(layer.opacity)/255.0;
        a.blend=layer.blend;
        archive.push_back(std::move(a));
    }
    Result<Bytes> encoded=error(ErrorCode::UnsupportedFeature,"export.format");
    if(plan.request.format==ExportFormat::Ora)
        encoded=encodeOra(plan.request.size,archive,merged,options);
    else if(plan.request.format==ExportFormat::LayerZip)
        encoded=encodeLayerZip(plan.request.size,archive,options);
    if(!encoded)return encoded.error();
    return writeRasterPlaceholder(plan,encoded.value(),stop);
}

}
