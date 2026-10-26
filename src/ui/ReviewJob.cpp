#include "ReviewJob.h"
#include "codecs/ImageReader.h"
#include "core/Commands.h"
#include "storage/ProjectCodec.h"
#include "models/AdapterRegistry.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
namespace drawborad {
Resource jsonResource(const QJsonObject& object) {
    const auto encoded=QJsonDocument(object).toJson(QJsonDocument::Compact);
    Bytes bytes(reinterpret_cast<const std::uint8_t*>(encoded.constData()),
                reinterpret_cast<const std::uint8_t*>(encoded.constData())+encoded.size());
    return {sha256(bytes),ResourceKind::Reference,std::make_shared<const Bytes>(std::move(bytes)),{}};
}
std::optional<QJsonObject> findJsonResource(const DocumentSnapshot& snapshot,const char* type) {
    for(const auto& [id,resource]:*snapshot.resources){
        (void)id;
        if(resource.kind!=ResourceKind::Reference || resource.bytes->size()>1024*1024) continue;
        const auto& bytes=*resource.bytes;
        const auto parsed=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())));
        if(parsed.isObject() && parsed.object().value("type").toString()==QLatin1String(type)) return parsed.object();
    }
    return {};
}
Result<QJsonObject> jobRequestMetadata(const DocumentSnapshot& snapshot,const char* type){
    std::optional<QJsonObject> found;
    for(const auto& [id,resource]:*snapshot.resources){
        (void)id;if(resource.kind!=ResourceKind::Reference || resource.bytes->size()>1024*1024)continue;
        const auto& b=*resource.bytes;const auto json=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
        if(!json.isObject() || json.object().value("type").toString()!=QLatin1String(type))continue;
        if(found)return error(ErrorCode::InvalidInput,"workflow.ambiguous-job-request");
        found=json.object();
    }
    if(!found)return error(ErrorCode::InvalidInput,"workflow.missing-job-request");
    return *found;
}
Result<std::optional<PromptSet>> reviewPromptRequest(const QJsonObject& object,PixelSize size){
    const auto mode=object.value("proposalMode").toString("automatic");
    if(mode=="automatic"){
        if(object.contains("points") || object.contains("box"))return error(ErrorCode::InvalidInput,"review.prompt-mode-required");
        return std::optional<PromptSet>{};
    }
    if(mode!="prompted" || !size.width || !size.height)return error(ErrorCode::InvalidInput,"review.proposal-mode");
    const auto points=object.value("points").toArray();
    if((object.contains("points") && !object.value("points").isArray()) || points.size()>64)return error(ErrorCode::BudgetExceeded,"review.prompt-points");
    PromptSet prompts;
    for(const auto& value:points){
        const auto point=value.toObject();const auto x=point.value("x").toDouble(-1),y=point.value("y").toDouble(-1);
        if(!value.isObject() || !point.value("x").isDouble() || !point.value("y").isDouble() || !point.value("positive").isBool() ||
           !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=double(size.width) || y>=double(size.height))return error(ErrorCode::InvalidInput,"review.prompt-point");
        prompts.points.push_back({x,y,point.value("positive").toBool()});
    }
    if(object.contains("box")){
        const auto box=object.value("box").toArray();
        if(!object.value("box").isArray() || box.size()!=4)return error(ErrorCode::InvalidInput,"review.prompt-box");
        for(const auto& value:box)if(!value.isDouble() || !std::isfinite(value.toDouble()))return error(ErrorCode::InvalidInput,"review.prompt-box");
        const auto x0=box[0].toDouble(),y0=box[1].toDouble(),x1=box[2].toDouble(),y1=box[3].toDouble();
        if(x0<0 || y0<0 || x1<=x0 || y1<=y0 || x1>double(size.width) || y1>double(size.height))return error(ErrorCode::InvalidInput,"review.prompt-box");
        prompts.boxes.push_back({x0,y0,x1,y1});
    }
    if(prompts.points.empty() && prompts.boxes.empty())return error(ErrorCode::InvalidInput,"review.prompt-required");
    return std::optional<PromptSet>{std::move(prompts)};
}
Result<Bytes> runModelPreflightJob(const DocumentSnapshot& input,std::stop_token stop,AnalysisProgress progress) {
    const auto request=jobRequestMetadata(input,ModelPreflightRequestType);if(!request)return request.error();
    const auto package=request.value().value("package").toString();
    if(progress)progress(0,1);
    const auto checked=preflightModelPackage(std::filesystem::path(package.toStdWString()),DevicePreference::CPU,stop);
    if(!checked)return checked.error();
    const auto& report=checked.value();const auto state=modelReadinessStateName(report.state);
    QJsonObject result{{"type",ModelPreflightResultType},{"receiptVersion",1},{"package",package},
        {"state",QString::fromUtf8(state.data(),qsizetype(state.size()))},
        {"messageKey",QString::fromStdString(report.messageKey)},{"diagnostic",QString::fromStdString(report.diagnostic)},
        {"provider",report.device==DevicePreference::CPU?"CPU":"CUDA"},{"probeKind",QString::fromStdString(report.probeKind)},
        {"elapsedUs",QString::number(report.elapsedUs)},{"observedTensorBytes",QString::number(report.observedTensorBytes)}};
    if(report.manifest){
        const auto& model=*report.manifest;
        result.insert("manifestHash",QString::fromStdString(model.manifestSha256.hex()));
        result.insert("encoderHash",QString::fromStdString(model.sha256.hex()));
        result.insert("decoderHash",QString::fromStdString(model.decoderSha256.hex()));
        result.insert("task",QString::fromStdString(model.taskId));result.insert("abi",QString::fromStdString(model.abi));
        result.insert("declaredProvider",QString::fromStdString(model.provider));result.insert("license",QString::fromStdString(model.license));
        result.insert("source",QString::fromStdString(model.source));result.insert("sourceCommit",QString::fromStdString(model.sourceCommit));
        result.insert("checkpointSha256",QString::fromStdString(model.checkpointSha256));result.insert("variant",QString::fromStdString(model.variant));
        result.insert("networkAccess",model.networkAccess);
    }
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"model.cancelled");
    auto resources=std::make_shared<ResourceMap>(*input.resources);const auto metadata=jsonResource(result);resources->emplace(metadata.id,metadata);
    auto output=input;output.resources=std::move(resources);output.revision=input.revision+1;
    if(progress)progress(1,1);
    return encodeDelta(input,output);
}
Result<Bytes> runImageReviewJob(const DocumentSnapshot& input,std::stop_token stop,AnalysisProgress progress) {
    const auto metadata=jobRequestMetadata(input,ReviewRequestType);
    if(!metadata)return metadata.error();
    const auto path=metadata.value().value("image").toString();
    const auto package=metadata.value().value("package").toString();
    const QFileInfo imageFile(path);
    if(!imageFile.isFile() || imageFile.size()<=0 || imageFile.size()>32*1024*1024)
        return error(ErrorCode::BudgetExceeded,"review.image-limit");
    auto model=validatePackage(std::filesystem::path(package.toStdWString()));
    if(!model) return model.error();
    const auto receipt=metadata.value().value("modelReadiness").toObject();
    if(receipt.value("state").toString()!="Ready" || receipt.value("receiptVersion").toInt()!=1 ||
       receipt.value("provider").toString()!="CPU" || receipt.value("probeKind").toString()!="deterministic-smoke" ||
       receipt.value("package").toString()!=package ||
       receipt.value("manifestHash").toString().toStdString()!=model.value().manifestSha256.hex() ||
       receipt.value("encoderHash").toString().toStdString()!=model.value().sha256.hex() ||
       receipt.value("decoderHash").toString().toStdString()!=model.value().decoderSha256.hex())
        return error(ErrorCode::InvalidInput,"review.model-preflight-required-or-stale");
    if(stop.stop_requested()) return error(ErrorCode::Cancelled,"review.cancelled");
    auto image=decodeImage(std::filesystem::path(path.toStdWString()),{},stop);
    if(!image) return image.error();
    if(!image.value().decodedPixels) return error(ErrorCode::InvalidInput,"review.no-decoded-image");
    if(image.value().info.format!=ImageFormat::Png && image.value().info.format!=ImageFormat::Jpeg)
        return error(ErrorCode::UnsupportedFeature,"review.png-jpeg-required");
    const auto size=image.value().info.orientedSize;
    // Full-frame native masks must fit in the existing 64 MiB sealed delta.
    // No silent downscaling or loss of source dimensions.
    if(!size.width || !size.height || size.width>4096 || size.height>4096 || size.width*size.height>4*1024*1024)
        return error(ErrorCode::BudgetExceeded,"review.image-pixel-limit");
    const auto& tile=*image.value().decodedPixels;
    if(tile.format.mode!=ColorMode::RGB || tile.format.depth!=Depth::U8 ||
       tile.width!=size.width || tile.height!=size.height)
        return error(ErrorCode::UnsupportedFeature,"review.rgb8-required");
    AnalysisRequest request; request.model=model.value(); request.sourceSize=size;
    request.view.rgbTiles={tile}; request.inputRevision=input.revision;
    if(tile.format.alpha){
        Bytes alpha(std::size_t(size.width*size.height));
        for(std::uint64_t y=0;y<size.height;++y) for(std::uint64_t x=0;x<size.width;++x)
            alpha[std::size_t(y*size.width+x)]=tile.bytes[std::size_t(y*tile.rowStride+x*4+3)];
        if(std::any_of(alpha.begin(),alpha.end(),[](auto value){return value!=0 && value!=255;}))
            return error(ErrorCode::UnsupportedFeature,"review.partial-alpha-not-supported");
        request.view.sourceAlpha={std::move(alpha)};
    }
    const auto prompts=reviewPromptRequest(metadata.value(),size);if(!prompts)return prompts.error();
    if(prompts.value())request.prompts=*prompts.value();
    const auto expectedHash=metadata.value().value("expectedSourceHash").toString();
    if(!expectedHash.isEmpty() && expectedHash.toStdString()!=image.value().info.sourceHash.hex())return error(ErrorCode::DigestMismatch,"review.source-changed");
    if(prompts.value() && progress)progress(0,1);
    auto proposals=prompts.value()?ProposalEngine{}.propose(request,stop):ProposalEngine{}.proposeAutomatic(request,{},stop,progress);
    if(prompts.value() && progress)progress(1,1);
    if(!proposals) return proposals.error();
    if(proposals.value().candidates.empty()) return error(ErrorCode::InvalidInput,"review.no-stable-candidates");
    if((proposals.value().candidates.size()+1)*size.width*size.height>56*1024*1024)
        return error(ErrorCode::BudgetExceeded,"review.mask-delta-limit");
    auto masks=projectMasks(proposals.value(),{size,true});
    if(!masks) return masks.error();
    auto resources=std::make_shared<ResourceMap>(*input.resources);
    // The source is provenance only: path/hash/size, never input raster pixels.
    auto source=jsonResource({{"type",SourceReferenceType},{"image",path},
        {"sourceHash",QString::fromStdString(image.value().info.sourceHash.hex())},
        {"modelHash",QString::fromStdString(model.value().sha256.hex())},
        {"modelReadiness",receipt},
        {"width",qint64(size.width)},{"height",qint64(size.height)}});
    resources->emplace(source.id,source);
    QJsonArray candidates;
    for(std::size_t i=0;i<masks.value().fullResolutionMasks.size();++i){
        if(stop.stop_requested()) return error(ErrorCode::Cancelled,"review.cancelled");
        auto bytes=std::move(masks.value().fullResolutionMasks[i]);
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(std::move(bytes)),{source.id}};
        resources->emplace(mask.id,mask);
        candidates.append(QJsonObject{{"mask",QString::fromStdString(mask.id.hex())},
            {"score",proposals.value().candidates[i].score}});
    }
    auto info=jsonResource({{"type",ReviewResultType},{"candidates",candidates},
        {"proposalMode",prompts.value()?"prompted":"automatic"},
        {"points",metadata.value().value("points")},{"box",metadata.value().value("box")},
        {"width",qint64(size.width)},{"height",qint64(size.height)}});
    resources->emplace(info.id,info);
    auto output=input; output.resources=std::move(resources); output.revision=input.revision+1;
    return encodeDelta(input,output);
}
}
