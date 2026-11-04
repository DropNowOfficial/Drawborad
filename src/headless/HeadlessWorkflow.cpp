#include "HeadlessWorkflow.h"
#include "codecs/ImageReader.h"
#include "color/ProfileStore.h"
#include "core/Commands.h"
#include "review/ReviewSession.h"
#include "storage/ProjectCodec.h"
#include "ui/ReconstructionJob.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <set>
namespace drawborad {
Result<HeadlessWorkflowReport> runHeadlessWorkflow(const HeadlessWorkflowRequest& request,std::stop_token stop) {
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    const auto& input=request.input;
    const auto size=input.masks.dimensions;
    const auto count=input.masks.fullResolutionMasks.size();
    if((input.backend!="fixture-no-ai" && input.backend!="sam3-image") ||
       !size.width || !size.height || size.width>4096 || size.height>4096 || size.width*size.height>4*1024*1024 ||
       !count || count>64 || count*size.width*size.height>56*1024*1024 ||
       input.proposals.sourceSize!=size || input.proposals.candidates.size()!=count ||
       input.names.size()!=count || input.background.size()!=count || input.scores.size()!=count)
        return error(ErrorCode::InvalidInput,"headless.input-layout");
    if(request.accepted.empty())return error(ErrorCode::InvalidInput,"headless.acceptance-required");
    std::set<std::size_t> accepted;
    for(const auto i:request.accepted)
        if(i>=count || !accepted.insert(i).second)return error(ErrorCode::InvalidInput,"headless.acceptance-index");
    Bytes occupied(std::size_t(size.width*size.height));
    for(std::size_t i=0;i<count;++i){
        const auto& mask=input.masks.fullResolutionMasks[i];
        if(mask.size()!=occupied.size() || input.names[i].isEmpty() || input.names[i].size()>128 ||
           !std::isfinite(input.scores[i]) || input.scores[i]<0 || input.scores[i]>1)
            return error(ErrorCode::InvalidInput,"headless.object-layout");
        bool nonempty=false;
        for(std::size_t p=0;p<mask.size();++p){
            if(mask[p]!=0 && mask[p]!=255)return error(ErrorCode::InvalidInput,"headless.mask-binary");
            if(mask[p]){
                if(occupied[p])return error(ErrorCode::InvalidInput,"headless.mask-overlap");
                occupied[p]=255;nonempty=true;
            }
        }
        if(!nonempty)return error(ErrorCode::InvalidInput,"headless.mask-empty");
    }
    const auto sourcePath=input.sourceReference.value("image").toString();
#ifdef _WIN32
    const auto nativePath=std::filesystem::path(sourcePath.toStdWString());
#else
    const auto nativePath=std::filesystem::path(sourcePath.toUtf8().toStdString());
#endif
    auto source=decodeImage(nativePath,{},stop);if(!source)return source.error();
    if(!source.value().decodedPixels || source.value().info.orientedSize!=size ||
       input.sourceReference.value("type").toString()!=SourceReferenceType ||
       input.sourceReference.value("sourceHash").toString().toStdString()!=source.value().info.sourceHash.hex() ||
       input.sourceReference.value("modelBackend").toString()!=input.backend)
        return error(ErrorCode::DigestMismatch,"headless.source-verification");
    const auto& pixels=*source.value().decodedPixels;
    if(pixels.format.mode!=ColorMode::RGB || pixels.format.depth!=Depth::U8)
        return error(ErrorCode::UnsupportedFeature,"headless.rgb8-required");
    for(std::uint64_t y=0;y<size.height;++y)for(std::uint64_t x=0;x<size.width;++x){
        const bool visible=!pixels.format.alpha || pixels.bytes[std::size_t(y*pixels.rowStride+x*4+3)]!=0;
        if((occupied[std::size_t(y*size.width+x)]!=0)!=visible)
            return error(ErrorCode::InvalidInput,"headless.source-coverage");
    }
    auto review=ReviewSession::create(input.proposals,input.masks,0);if(!review)return review.error();
    auto reviewed=review.value().accept(0);if(!reviewed)return reviewed.error();
    NewDocumentRequest create;create.dimensions={double(size.width),double(size.height),Unit::Px,300};
    create.colorSpec.profileId=source.value().info.icc;
    auto spec=validateDocument(create,{});if(!spec)return spec.error();
    Id documentId;std::copy_n(source.value().info.sourceHash.bytes.begin(),16,documentId.bytes.begin());
    auto document=Document::create(spec.value(),documentId);if(!document)return document.error();
    const auto reference=jsonResource(input.sourceReference);
    Command setup;setup.operations.emplace_back(InstallResource{reference});
    if(!source.value().info.icc.empty()){
        const auto& bytes=source.value().info.iccProfile;auto profile=inspectIcc(bytes);
        if(!profile || profile.value().colorSpace!=IccColorSpace::RGB || sha256(bytes)!=source.value().info.icc)
            return error(ErrorCode::InvalidProfile,"headless.source-profile");
        setup.operations.emplace_back(InstallResource{Resource{source.value().info.icc,ResourceKind::Profile,std::make_shared<const Bytes>(bytes),{}}});
    }
    QJsonArray names;
    // Preserve manifest's back-to-front order, independent of selection order.
    for(std::size_t i=0;i<count;++i){
        if(!accepted.contains(i))continue;
        const auto& bytes=reviewed.value().masks[i];
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{reference.id}};
        setup.operations.emplace_back(InstallResource{mask});
        LayerNode object;object.id=Id::fromNumber(10+i*2);object.kind=NodeKind::Object;
        object.parent=Document::rootId();object.name=input.names[i].toUtf8().toStdString();object.maskId=mask.id;
        object.bounds={0,0,size.width,size.height};
        LayerNode paint;paint.id=Id::fromNumber(11+i*2);paint.parent=object.id;paint.objectId=object.id;
        paint.name=object.name+" / Paint";paint.stage="paint";paint.bounds=object.bounds;
        setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{paint});names.append(input.names[i]);
    }
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    auto installed=document.value().apply(setup,0);if(!installed)return installed.error();
    const auto before=document.value().snapshot();auto job=before;
    auto resources=std::make_shared<ResourceMap>(*before.resources);
    const auto instruction=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",sourcePath}});
    resources->emplace(instruction.id,instruction);job.resources=resources;
    auto delta=runReconstructionJob(job,stop);if(!delta)return delta.error();
    auto after=decodeDelta(delta.value(),job);if(!after)return after.error();
    auto command=reconstructionCommand(before,after.value());if(!command)return command.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"headless.cancelled");
    auto committed=document.value().apply(command.value(),before.revision);if(!committed)return committed.error();
    HeadlessWorkflowReport report;report.snapshot=document.value().snapshot();
    report.metadata=findJsonResource(report.snapshot,ReconstructionResultType).value_or(QJsonObject{});
    report.metadata.insert("backend",input.backend);report.metadata.insert("acceptedObjects",names);
    report.metadata.insert("strokeEvents",qint64(report.snapshot.eventIndex->size()));
    report.metadata.insert("objectCount",qint64(accepted.size()));
    return report;
}
}
