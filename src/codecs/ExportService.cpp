#include "ExportService.h"
#include "storage/FileIO.h"
#include "codecs/AtomicExport.h"
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
}
