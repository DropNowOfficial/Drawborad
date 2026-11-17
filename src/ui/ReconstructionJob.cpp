#include "ReconstructionJob.h"
#include "PaintedDocument.h"
#include "codecs/ImageReader.h"
#include "reconstruct/Planning.h"
#include "reconstruct/Refinement.h"
#include "render/ReferenceRenderer.h"
#include "storage/ProjectCodec.h"
#include "view/LayerTree.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <limits>
#include <set>

namespace drawborad {
namespace {
std::optional<QJsonObject> metadata(const Resource& resource,const char* type){
    if(resource.kind!=ResourceKind::Reference || resource.bytes->size()>1024*1024)return {};
    const auto& b=*resource.bytes;
    const auto json=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
    if(!json.isObject() || json.object().value("type").toString()!=QLatin1String(type))return {};
    return json.object();
}
Id stageId(const DocumentSnapshot& snapshot,NodeId object,const char* stage){
    Bytes bytes(snapshot.documentId.bytes.begin(),snapshot.documentId.bytes.end());
    bytes.insert(bytes.end(),object.bytes.begin(),object.bytes.end());
    for(unsigned i=0;i<8;++i)bytes.push_back(std::uint8_t(snapshot.revision>>(8*i)));
    for(const auto* p=stage;*p;++p)bytes.push_back(std::uint8_t(*p));
    const auto hash=sha256(bytes);Id id;std::copy_n(hash.bytes.begin(),16,id.bytes.begin());return id;
}
std::uint64_t seedFor(Id id){
    std::uint64_t seed=0;for(unsigned i=0;i<8;++i)seed=(seed<<8)|id.bytes[i];return seed;
}
}
Result<Bytes> runReconstructionJob(const DocumentSnapshot& input,std::stop_token stop,AnalysisProgress progress){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.cancelled");
    const auto verified=jobRequestMetadata(input,ReconstructionRequestType);if(!verified)return verified.error();
    const auto request=std::optional<QJsonObject>{verified.value()};
    const auto mode=request->value("mode").toString();const bool refine=mode=="refine";
    if(mode!="reconstruct" && !refine)return error(ErrorCode::InvalidInput,"reconstruct.job-request");
    const auto planner=request->value("planner").toString("sweep");
    if(planner!="sweep" && planner!="multiscale")return error(ErrorCode::InvalidInput,"reconstruct.planner");
    const bool multiscale=planner=="multiscale";
    const std::uint64_t planningEvents=multiscale?32768:8192;
    const std::uint64_t planningSamples=multiscale?1000000:250000;
    NodeId selected;
    Rect roi{0,0,input.spec.pixelSize.width,input.spec.pixelSize.height};
    if(refine){
        const auto text=request->value("object").toString();const auto bytes=QByteArray::fromHex(text.toLatin1());
        if(text.size()!=32 || bytes.size()!=16 || QString::fromLatin1(bytes.toHex())!=text.toLower())return error(ErrorCode::InvalidInput,"reconstruct.refine-object");
        std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()),16,selected.bytes.begin());
        const auto x=request->value("x").toInteger(-1),y=request->value("y").toInteger(-1);
        const auto w=request->value("width").toInteger(-1),h=request->value("height").toInteger(-1);
        if(x<0 || y<0 || w<=0 || h<=0 || std::uint64_t(x)>=roi.width || std::uint64_t(y)>=roi.height ||
           std::uint64_t(w)>roi.width-std::uint64_t(x) || std::uint64_t(h)>roi.height-std::uint64_t(y))
            return error(ErrorCode::InvalidInput,"reconstruct.refine-region");
        roi={x,y,std::uint64_t(w),std::uint64_t(h)};
    }
    auto valid=validateSnapshot(input);if(!valid)return valid.error();
    const auto path=request->value("image").toString();const QFileInfo file(path);
    if(!file.isFile() || file.size()<=0 || file.size()>32*1024*1024)return error(ErrorCode::InvalidInput,"reconstruct.source-missing");
    auto image=decodeImage(std::filesystem::path(path.toStdWString()),{},stop);if(!image)return image.error();
    if(!image.value().decodedPixels)return error(ErrorCode::InvalidInput,"reconstruct.source-decode");
    auto source=std::move(*image.value().decodedPixels);
    const auto size=input.spec.pixelSize;
    if(size.width>4096 || size.height>4096 || size.width*size.height>4*1024*1024 ||
       source.width!=size.width || source.height!=size.height ||
       input.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true} ||
       source.format.mode!=ColorMode::RGB || source.format.depth!=Depth::U8 ||
       source.profileId!=input.spec.colorSpec.profileId)
        return error(ErrorCode::UnsupportedFeature,"reconstruct.source-layout");
    // Add an opaque alpha channel only; this is not a color/profile conversion.
    if(!source.format.alpha){
        Bytes rgba(std::size_t(size.width*size.height*4),255);
        for(std::uint64_t y=0;y<size.height;++y)for(std::uint64_t x=0;x<size.width;++x)
            std::copy_n(source.bytes.begin()+std::ptrdiff_t(y*source.rowStride+x*3),3,rgba.begin()+std::ptrdiff_t((y*size.width+x)*4));
        source.format.alpha=true;source.rowStride=size.width*4;source.bytes=std::move(rgba);
    }
    std::vector<NodeId> objects;
    ReviewedObjects reviewed;reviewed.confirmedRevision=input.revision;reviewed.canReconstruct=true;
    std::vector<NodeId> ordered,pending{Document::rootId()};
    while(!pending.empty()){
        const auto id=pending.back();pending.pop_back();const auto& node=input.nodes->at(id);
        if(node.kind==NodeKind::Object)ordered.push_back(id);
        for(auto it=node.children.rbegin();it!=node.children.rend();++it)pending.push_back(*it);
    }
    for(const auto id:ordered){
        const auto& node=input.nodes->at(id);
        if(node.kind!=NodeKind::Object || node.maskId.empty())continue;
        if(refine && id!=selected)continue;
        const auto mask=input.resources->find(node.maskId);
        if(mask==input.resources->end() || mask->second.kind!=ResourceKind::Mask || mask->second.bytes->size()!=size.width*size.height)
            return error(ErrorCode::InvalidInput,"reconstruct.review-mask-layout");
        bool approvedSource=false;
        for(const auto dependency:mask->second.dependencies){
            const auto found=input.resources->find(dependency);if(found==input.resources->end())continue;
            const auto info=metadata(found->second,SourceReferenceType);
            if(info && info->value("sourceHash").toString().toStdString()==image.value().info.sourceHash.hex())approvedSource=true;
        }
        if(!approvedSource)return error(ErrorCode::InvalidInput,"reconstruct.source-hash-mismatch");
        if(node.locked || !node.visible || !layerWritable(input,id) || !nativeCompositingSupported(input,id))
            return error(ErrorCode::UnsupportedFeature,"reconstruct.object-not-editable");
        objects.push_back(id);reviewed.masks.push_back(*mask->second.bytes);
    }
    if(objects.empty() || objects.size()>256)return error(ErrorCode::InvalidInput,"reconstruct.no-reviewed-objects");
    Command command;
    auto nodes=std::make_shared<NodeMap>(*input.nodes);
    if(refine)for(const auto object:objects){
        const auto& children=nodes->at(object).children;
        if(std::none_of(children.begin(),children.end(),[&](NodeId child){return nodes->at(child).kind==NodeKind::Layer && nodes->at(child).stage=="detail";}))
            return error(ErrorCode::InvalidInput,"reconstruct.refine-detail-required");
        LayerNode layer;layer.id=stageId(input,object,"refine-pass");layer.parent=object;layer.objectId=object;
        layer.stage="detail";layer.name="Local refinement";layer.bounds={0,0,size.width,size.height};
        if(nodes->contains(layer.id))return error(ErrorCode::InvalidInput,"reconstruct.stage-id-collision");
        // A requested correction must be above every existing stage/manual
        // layer. Retain the old strokes; one new pass remains independently undoable.
        command.operations.emplace_back(AddNode{layer});nodes->emplace(layer.id,layer);nodes->at(object).children.push_back(layer.id);
    }
    else for(const auto object:objects){
        std::size_t index=0;
        for(const auto* stage:{"guides","base","detail"}){
            LayerNode layer;layer.id=stageId(input,object,stage);layer.parent=object;layer.objectId=object;
            layer.name=std::string(std::string_view(stage)=="guides"?"Guides":std::string_view(stage)=="base"?"Base pigment":"Local refinement");
            layer.stage=stage;layer.bounds={0,0,size.width,size.height};
            if(nodes->contains(layer.id))return error(ErrorCode::InvalidInput,"reconstruct.stage-id-collision");
            command.operations.emplace_back(AddNode{layer,index});nodes->emplace(layer.id,layer);
            nodes->at(object).children.insert(nodes->at(object).children.begin()+std::ptrdiff_t(index++),layer.id);
        }
    }
    auto planning=input;planning.nodes=nodes;
    std::uint64_t nextTime=100;
    for(const auto& event:*input.eventIndex){
        if(event.endUs>std::numeric_limits<std::uint64_t>::max()-1000000)return error(ErrorCode::BudgetExceeded,"reconstruct.schedule-time");
        nextTime=std::max(nextTime,event.endUs+100);
    }
    std::size_t eventCount=0;QJsonArray objectResults;
    for(std::size_t i=0;i<objects.size();++i){
        const auto object=objects[i];
        std::vector<EventBatch> generatedBatches;
        if(refine){
            const auto target=stageId(input,object,"refine-pass");
            if(!nativeLayerPaintable(planning,target))return error(ErrorCode::InvalidInput,"reconstruct.refine-detail-required");
            auto measured=planning;auto events=std::make_shared<EventList>();
            // Flatten only normal, fully opaque source-free stage layers for
            // the existing single-layer error estimator. Per-layer erasers or
            // opacity require offscreen compositing and are rejected explicitly.
            for(const auto layer:layerOrder(input)){
                const auto& node=nodes->at(layer);if(node.objectId!=object || !layerVisible(input,layer))continue;
                if(node.opacity!=1 || !nativeLayerSupported(input,layer))return error(ErrorCode::UnsupportedFeature,"reconstruct.refine-compositing");
                for(const auto& event:*input.eventIndex)if(event.layerId==layer){
                    if(event.toolId=="eraser")return error(ErrorCode::UnsupportedFeature,"reconstruct.refine-layer-eraser");
                    auto copy=event;copy.layerId=target;events->push_back(std::move(copy));
                }
            }
            measured.eventIndex=events;
            PlanningRegion region{planning,object,target,roi,reviewed.masks[i],&source,seedFor(stageId(input,object,"refine")),4096,250000,stop};
            RefinementRequest refinement{region,measured,{},stop};
            auto generated=refineRegion(refinement,stop);if(!generated)return generated.error();
            if(generated.value().events.empty())return error(ErrorCode::InvalidInput,"reconstruct.region-already-converged");
            generatedBatches.push_back(std::move(generated.value()));
        }else for(const auto* stage:{"guides","base"}){
            if(stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.cancelled");
            const auto layer=stageId(input,object,stage);
            PlanningRegion region{planning,object,layer,{0,0,size.width,size.height},reviewed.masks[i],&source,seedFor(layer),planningEvents,planningSamples,stop};
            region.clipToReviewedMask=std::string_view(stage)=="base";
            auto generated=std::string_view(stage)=="guides"?planContours(region):
                multiscale?planMultiscalePigment(region):planBasePigment(region,StyleSettings{"marker",2,1,1,.04});
            // Guides are optional for a thin visible object: conservative nib
            // erosion may leave no contour while its clipped pigment is valid.
            // All other guide errors, including cancellation/budgets, propagate.
            if(!generated && std::string_view(stage)=="guides" && generated.error().code==ErrorCode::InvalidInput &&
               generated.error().messageKey=="reconstruct.empty-safe-region")continue;
            if(!generated)return generated.error();
            if(generated.value().events.empty())continue;
            generatedBatches.push_back(std::move(generated.value()));
        }
        for(auto& batch:generatedBatches){
            const auto layer=batch.layerId;
            SchedulePolicy policy;policy.seed=seedFor(stageId(input,layer,refine?"refine-schedule":"schedule"));policy.startUs=nextTime;
            if(!refine)policy.maxEvents=planningEvents;
            auto scheduled=scheduleStrokes(batch,policy);if(!scheduled)return scheduled.error();
            ReconstructionRequest compile;compile.snapshot=planning;compile.reviewed=reviewed;compile.source={&source};
            compile.maskIndex=i;compile.objectId=object;compile.layerId=layer;compile.proposedEvents=std::move(scheduled.value().events);
            if(!refine){compile.budget.maxEvents=planningEvents;compile.budget.maxSamples=planningSamples;}
            auto compiled=ReconstructionService{}.compile(compile,stop);if(!compiled)return compiled.error();
            nextTime=compiled.value().staged.events.back().endUs+100;
            eventCount+=compiled.value().staged.events.size();
            if(eventCount>(refine?4096u:multiscale?65536u:16384u))return error(ErrorCode::BudgetExceeded,"reconstruct.job-event-budget");
            for(auto& event:compiled.value().staged.events){auto bounds=strokeBounds(event);if(!bounds)return bounds.error();command.operations.emplace_back(Paint{std::move(event),bounds.value()});}
        }
        objectResults.append(QJsonObject{{"name",QString::fromUtf8(nodes->at(object).name)},{"status","needs-review"}});
        if(progress)progress(std::uint32_t(i+1),std::uint32_t(objects.size()));
    }
    if(eventCount==0)return error(ErrorCode::InvalidInput,"reconstruct.no-plannable-strokes");
    auto document=Document::fromSnapshot(input);if(!document)return document.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.cancelled");
    const auto applied=document.value().apply(command,input.revision);if(!applied)return applied.error();
    auto output=document.value().snapshot();
    auto painted=renderPaintedDocument(output,{output.revision},stop);if(!painted)return painted.error();
    std::uint64_t measured=0;double colorSum=0,alphaSum=0;
    for(std::uint64_t y=std::uint64_t(roi.y);y<std::uint64_t(roi.y)+roi.height;++y){
        if((y&63)==0 && stop.stop_requested())return error(ErrorCode::Cancelled,"reconstruct.cancelled");
        for(std::uint64_t x=std::uint64_t(roi.x);x<std::uint64_t(roi.x)+roi.width;++x){
            const auto p=std::size_t(y*size.width+x);
            if(std::none_of(reviewed.masks.begin(),reviewed.masks.end(),[&](const Bytes& mask){return mask[p]!=0;}))continue;
            ++measured;const auto at=std::size_t(y*source.rowStride+x*4);const auto alpha=source.bytes[at+3]/255.0;
            alphaSum+=std::abs(alpha-painted.value().merged.bytes[p*4+3]/255.0);
            for(unsigned c=0;c<3;++c)colorSum+=std::abs(int(source.bytes[at+c])-int(painted.value().merged.bytes[p*4+c]))/255.0*alpha/3.0;
        }
    }
    if(!measured)return error(ErrorCode::InvalidInput,"reconstruct.no-measured-region");
    auto info=jsonResource({{"type",ReconstructionResultType},{"mode",mode},{"planner",planner},{"planningEventBudgetPerLayer",qint64(planningEvents)},{"planningSampleBudgetPerLayer",qint64(planningSamples)},{"events",qint64(eventCount)},
        {"inputRevision",QString::number(input.revision)},
        {"sourceHash",QString::fromStdString(image.value().info.sourceHash.hex())},{"objects",objectResults},
        {"measuredPixels",qint64(measured)},{"acceptedImageFraction",double(measured)/double(size.width*size.height)},
        {"acceptedColorMae",colorSum/double(measured)},{"acceptedAlphaMae",alphaSum/double(measured)},
        {"sourceAssetReads",qint64(painted.value().statistics.sourceAssetReads)},
        {"status","needs-review"},{"wholeImageFidelityVerified",false},{"naturalProcessReviewed",false}});
    auto outputResources=std::make_shared<ResourceMap>(*output.resources);outputResources->emplace(info.id,info);output.resources=outputResources;
    return encodeDelta(input,output);
}
Result<Command> reconstructionCommand(const DocumentSnapshot& before,const DocumentSnapshot& after){
    if(after.documentId!=before.documentId || after.revision!=before.revision+1 || after.spec!=before.spec ||
       after.eventIndex->size()<=before.eventIndex->size() ||
       !std::equal(before.eventIndex->begin(),before.eventIndex->end(),after.eventIndex->begin()))
        return error(ErrorCode::StaleRevision,"reconstruct.result-base");
    auto valid=validateSnapshot(after);if(!valid)return valid.error();
    Command command;
    auto expectedResources=std::make_shared<ResourceMap>(*before.resources);
    for(const auto& [id,resource]:*before.resources){
        const auto found=after.resources->find(id);
        if(found==after.resources->end() || found->second!=resource)return error(ErrorCode::InvalidInput,"reconstruct.result-resource-rewrite");
    }
    bool resultMetadata=false;
    for(const auto& [id,resource]:*after.resources){
        if(before.resources->contains(id))continue;
        if(metadata(resource,ReconstructionRequestType))continue;
        if(!metadata(resource,ReconstructionResultType) || !resource.dependencies.empty())return error(ErrorCode::InvalidInput,"reconstruct.result-resource-kind");
        resultMetadata=true;command.operations.emplace_back(InstallResource{resource});expectedResources->emplace(id,resource);
    }
    if(!resultMetadata)return error(ErrorCode::InvalidInput,"reconstruct.result-metadata");
    for(const auto& [id,node]:*before.nodes){
        const auto found=after.nodes->find(id);if(found==after.nodes->end())return error(ErrorCode::InvalidInput,"reconstruct.result-node-rewrite");
        auto restored=found->second;restored.children.erase(std::remove_if(restored.children.begin(),restored.children.end(),
            [&](NodeId child){return !before.nodes->contains(child);}),restored.children.end());
        if(restored!=node)return error(ErrorCode::InvalidInput,"reconstruct.result-node-rewrite");
        for(std::size_t index=0;index<found->second.children.size();++index){
            const auto child=found->second.children[index];if(before.nodes->contains(child))continue;
            const auto& layer=after.nodes->at(child);
            if(node.kind!=NodeKind::Object || layer.kind!=NodeKind::Layer || layer.parent!=id || layer.objectId!=id ||
               !layer.children.empty() || !nativeLayerPaintable(after,child) ||
               (layer.stage!="guides" && layer.stage!="base" && layer.stage!="detail"))
                return error(ErrorCode::InvalidInput,"reconstruct.result-stage");
            command.operations.emplace_back(AddNode{layer,index});
        }
    }
    std::map<NodeId,EventBatch> batches;
    for(std::size_t i=before.eventIndex->size();i<after.eventIndex->size();++i){
        const auto& event=(*after.eventIndex)[i];
        const auto found=after.nodes->find(event.layerId);
        if(found==after.nodes->end() || found->second.objectId!=event.objectId || !nativeLayerPaintable(after,event.layerId) ||
           (before.nodes->contains(event.layerId) && found->second.stage!="detail"))
            return error(ErrorCode::InvalidInput,"reconstruct.result-event-target");
        auto [it,inserted]=batches.try_emplace(event.layerId,EventBatch{before.revision,event.objectId,event.layerId,{}});(void)inserted;
        it->second.events.push_back(event);
        auto bounds=strokeBounds(event);if(!bounds)return bounds.error();command.operations.emplace_back(Paint{event,bounds.value()});
    }
    for(const auto& [id,batch]:batches){(void)id;auto checked=validateReconstructionBatch(batch);if(!checked)return checked.error();}
    auto check=Document::fromSnapshot(before);if(!check)return check.error();
    auto applied=check.value().apply(command,before.revision);if(!applied)return applied.error();
    auto expected=after;expected.resources=expectedResources;
    if(!check.value().snapshot().sameContent(expected))return error(ErrorCode::InvalidInput,"reconstruct.result-unexpected-change");
    return command;
}
}
