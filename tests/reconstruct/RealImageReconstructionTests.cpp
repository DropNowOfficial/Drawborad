#include "Test.h"
#include "RealCoffeeFixture.h"
#include "ui/ReconstructionJob.h"
#include "ui/PaintedDocument.h"
#include "codecs/ImageReader.h"
#include "storage/ProjectCodec.h"
#include "storage/ProjectStore.h"
#include "render/ReferenceRenderer.h"
#include "codecs/RasterWriters.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <set>
using namespace drawborad;
namespace {
Bytes read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("missing approved fixture");const auto b=file.readAll();return Bytes(reinterpret_cast<const std::uint8_t*>(b.constData()),reinterpret_cast<const std::uint8_t*>(b.constData())+b.size());}
void exportManifest(const PaintedDocument& painted,const QString& path){
    QJsonArray layers;
    for(const auto& layer:painted.layers)layers.append(QJsonObject{{"name",QString::fromUtf8(layer.name)},
        {"pixelsSha256",QString::fromStdString(sha256(layer.pixels.bytes).hex())},{"maskSha256",QString::fromStdString(sha256(layer.mask).hex())},
        {"opacity",layer.opacity},{"visible",layer.visible},{"sectionType",layer.sectionType?int(*layer.sectionType):-1}});
    const auto json=QJsonDocument(QJsonObject{{"width",int(painted.merged.width)},{"height",int(painted.merged.height)},
        {"mergedSha256",QString::fromStdString(sha256(painted.merged.bytes).hex())},{"layers",layers}}).toJson();
    QFile file(path);CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(json)==json.size());
    QFile raw(path+".rgba");CHECK(raw.open(QIODevice::WriteOnly));CHECK(raw.write(reinterpret_cast<const char*>(painted.merged.bytes.data()),qint64(painted.merged.bytes.size()))==qint64(painted.merged.bytes.size()));
}
DocumentSnapshot coffeeDocument(){
    NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};
    const auto spec=validateDocument(request,{});CHECK(spec);
    auto document=Document::create(spec.value(),Id::fromNumber(900));CHECK(document);
    const auto sourcePath=qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE");
    CHECK(sha256(read(sourcePath)).hex()=="cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7");
    auto source=jsonResource({{"type",SourceReferenceType},{"image",sourcePath},{"sourceHash","cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7"},{"width",600},{"height",400}});
    Command command;command.operations.push_back(InstallResource{source});
    // Actual SAM masks recorded by the accepted native coffee inference test,
    // now explicitly selected as two reviewed object regions. No fake runner.
    for(const auto index:{3,5}){
        const auto bytes=fixture::realCoffeeMask(unsigned(index));
        CHECK(bytes.size()==240000);
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{source.id}};
        LayerNode object;object.id=Id::fromNumber(std::uint64_t(10+index));object.parent=Document::rootId();object.kind=NodeKind::Object;
        object.name=index==3?"Visible cup interior":"Coffee surface";object.maskId=mask.id;object.bounds={0,0,600,400};
        LayerNode layer;layer.id=Id::fromNumber(std::uint64_t(100+index));layer.parent=object.id;layer.objectId=object.id;layer.name="Manual paint";layer.stage="paint";layer.bounds=object.bounds;
        command.operations.push_back(InstallResource{mask});command.operations.push_back(AddNode{object});command.operations.push_back(AddNode{layer});
    }
    CHECK(document.value().apply(command,0));
    return document.value().snapshot();
}
DocumentSnapshot instruction(DocumentSnapshot snapshot,const QString& source){
    auto resources=std::make_shared<ResourceMap>(*snapshot.resources);
    auto request=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",source},{"seed",20261023}});
    resources->emplace(request.id,request);snapshot.resources=resources;return snapshot;
}
}
TEST("E07 accepted real coffee regions produce colored stage strokes from blank without source resources"){
    const auto before=coffeeDocument();const auto input=instruction(before,qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"));
    const auto result=runReconstructionJob(input);
    if(!result)throw std::runtime_error(result.error().messageKey+": "+result.error().diagnosticId);
    const auto output=decodeDelta(result.value(),input);CHECK(output);
    CHECK(before.eventIndex->empty());CHECK(output.value().revision==input.revision+1);
    CHECK(!output.value().eventIndex->empty());
    std::set<NodeId> objects,layers;
    std::size_t colored=0;
    for(const auto& event:*output.value().eventIndex){
        objects.insert(event.objectId);layers.insert(event.layerId);
        CHECK(event.parentRevision==input.revision);CHECK(event.engineVersion==2);
        CHECK(event.toolId=="guide" || event.toolId=="marker" || event.toolId=="colored-pencil");
        if(event.color.channels[0]-event.color.channels[2]>.05)++colored;
    }
    CHECK(objects.size()==2);CHECK(layers.size()>=4);CHECK(colored>0);
    for(const auto& [id,node]:*output.value().nodes){(void)id;CHECK(node.sourceResource.empty());CHECK(node.content.empty());}
    for(const auto& [id,resource]:*output.value().resources){(void)id;CHECK(resource.kind==ResourceKind::Mask || resource.kind==ResourceKind::Reference);}
    const auto report=findJsonResource(output.value(),ReconstructionResultType);CHECK(report);
    CHECK(report->contains("acceptedColorMae"));CHECK(report->contains("acceptedAlphaMae"));
    CHECK(report->value("measuredPixels").toInteger()==34731);
    CHECK(!report->value("wholeImageFidelityVerified").toBool());
    const auto command=reconstructionCommand(before,output.value());CHECK(command);
    auto committed=Document::fromSnapshot(before);CHECK(committed);
    CHECK(committed.value().apply(command.value(),before.revision));
    CHECK(committed.value().snapshot().eventIndex->size()==output.value().eventIndex->size());
    const auto painted=renderPaintedDocument(committed.value().snapshot(),{committed.value().snapshot().revision});
    if(!painted)throw std::runtime_error(painted.error().messageKey);
    CHECK(painted.value().statistics.sourceAssetReads==0);
    CHECK(painted.value().statistics.rasterizedEvents>0);
    std::size_t covered=0;
    for(std::size_t p=0;p<240000;++p){
        const bool inside=(*before.resources->at(before.nodes->at(Id::fromNumber(13)).maskId).bytes)[p] ||
                          (*before.resources->at(before.nodes->at(Id::fromNumber(15)).maskId).bytes)[p];
        const auto alpha=painted.value().merged.bytes[p*4+3];
        if(alpha)++covered;
        if(!inside)CHECK(alpha==0);
    }
    CHECK(covered>20000);
    const auto image=decodeImage(std::filesystem::path(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE").toStdWString()),{});CHECK(image);CHECK(image.value().decodedPixels);
    const auto& pixels=*image.value().decodedPixels;double colorError=0;std::size_t samples=0;
    for(std::size_t p=0;p<240000;++p){
        const bool inside=(*before.resources->at(before.nodes->at(Id::fromNumber(13)).maskId).bytes)[p] || (*before.resources->at(before.nodes->at(Id::fromNumber(15)).maskId).bytes)[p];
        if(!inside)continue;
        const auto at=(p/600)*pixels.rowStride+(p%600)*pixels.format.channelCount();
        for(unsigned c=0;c<3;++c){colorError+=std::abs(int(pixels.bytes[at+c])-int(painted.value().merged.bytes[p*4+c]))/255.0;++samples;}
    }
    std::cout<<"accepted region RGB MAE="<<colorError/double(samples)<<'\n';
    CHECK(colorError/double(samples)<.08); // Coarse-regression bound, not the E06 fidelity gate.
    const auto blank=renderPaintedDocument(committed.value().snapshot(),{committed.value().snapshot().revision,0});CHECK(blank);
    CHECK(std::all_of(blank.value().merged.bytes.begin(),blank.value().merged.bytes.end(),[](auto v){return v==0;}));
    PsdWriteRequest psd;psd.size=before.spec.pixelSize;psd.format=before.spec.colorSpec.format;
    psd.ppi=before.spec.ppi;
    psd.layers=painted.value().layers;psd.merged=painted.value().merged;
    const auto encoded=encodePsd(psd);CHECK(encoded);
    const auto decoded=decodePhotoshopLayers(encoded.value(),before.spec.colorSpec.profileId,{});CHECK(decoded);
    CHECK(decoded.value().layers.size()==painted.value().layers.size());CHECK(decoded.value().composite);
    const auto& preview=*decoded.value().composite;
    for(std::size_t p=0;p<240000;++p){
        const auto alpha=painted.value().merged.bytes[p*4+3];CHECK(preview.bytes[p*4+3]==alpha);
        for(unsigned c=0;c<3;++c){
            // PSD's 8-bit white-matted merged preview loses at most one
            // premultiplied sample; layer channel data below is lossless.
            CHECK(std::abs(int(preview.bytes[p*4+c])-int(painted.value().merged.bytes[p*4+c]))*alpha<=255);
        }
    }
    for(std::size_t i=0;i<painted.value().layers.size();++i){
        CHECK(decoded.value().layers[i].pixels.bytes==painted.value().layers[i].pixels.bytes);
        CHECK(decoded.value().layers[i].mask==painted.value().layers[i].mask);
        CHECK(decoded.value().layers[i].info.name==painted.value().layers[i].name);
    }
    std::size_t groups=0,masks=0;
    for(const auto& layer:decoded.value().layers){if(layer.info.sectionType==2)++groups;if(!layer.mask.empty())++masks;}
    CHECK(groups==2);CHECK(masks>=4);
    const auto artifacts=qEnvironmentVariable("DRAWBORAD_RECONSTRUCTION_ARTIFACTS");
    if(!artifacts.isEmpty()){
        CHECK(QDir().mkpath(artifacts));
        exportManifest(painted.value(),QDir(artifacts).filePath("coffee-coarse-export.json"));
        const auto png=encodeRaster(ExportFormat::Png,painted.value().merged,{});CHECK(png);
        const auto delta=encodeDelta({},committed.value().snapshot());CHECK(delta);
        for(const auto& [name,bytes]:std::vector<std::pair<QString,Bytes>>{{"coffee-coarse-strokes.png",png.value()},{"coffee-coarse-layers.psd",encoded.value()},{"coffee-coarse.snapshot.delta",delta.value()}}){
            QFile file(QDir(artifacts).filePath(name));CHECK(file.open(QIODevice::WriteOnly));
            CHECK(file.write(reinterpret_cast<const char*>(bytes.data()),qint64(bytes.size()))==qint64(bytes.size()));
        }
        std::cout<<"real reconstruction events="<<output.value().eventIndex->size()<<" paintedPixels="<<covered<<" sourceAssetReads="<<painted.value().statistics.sourceAssetReads<<'\n';
    }
}
TEST("E07 missing source and cancellation never produce a paint transaction"){
    const auto before=coffeeDocument();
    const auto missing=runReconstructionJob(instruction(before,"Z:/missing-coffee.png"));CHECK(!missing);
    std::stop_source stop;stop.request_stop();
    const auto cancelled=runReconstructionJob(instruction(before,qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")),stop.get_token());
    CHECK(!cancelled);CHECK(cancelled.error().code==ErrorCode::Cancelled);
    CHECK(before.eventIndex->empty());
    auto locked=before;auto nodes=std::make_shared<NodeMap>(*locked.nodes);nodes->at(Id::fromNumber(13)).locked=true;locked.nodes=nodes;
    CHECK(!runReconstructionJob(instruction(locked,qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"))));
    auto mismatch=before;auto resources=std::make_shared<ResourceMap>(*before.resources);
    const auto wrong=jsonResource({{"type",SourceReferenceType},{"image",qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")},{"sourceHash",QString(64,'0')}});resources->emplace(wrong.id,wrong);
    for(auto& [id,resource]:*resources){(void)id;if(resource.kind==ResourceKind::Mask)resource.dependencies={wrong.id};}mismatch.resources=resources;
    const auto wrongSource=runReconstructionJob(instruction(mismatch,qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")));CHECK(!wrongSource);CHECK(wrongSource.error().messageKey=="reconstruct.source-hash-mismatch");
}
TEST("E07 regional refinement preserves other objects and every pixel outside the requested rectangle"){
    const auto blank=coffeeDocument();const auto input=instruction(blank,qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"));
    const auto delta=runReconstructionJob(input);CHECK(delta);const auto out=decodeDelta(delta.value(),input);CHECK(out);
    auto document=Document::fromSnapshot(blank);CHECK(document);const auto initial=reconstructionCommand(blank,out.value());CHECK(initial);
    CHECK(document.value().apply(initial.value(),blank.revision));
    const auto previous=document.value().snapshot();StrokeEvent manual;manual.id=Id::fromNumber(50000);manual.objectId=Id::fromNumber(15);manual.layerId=Id::fromNumber(105);
    manual.parentRevision=previous.revision;manual.engineVersion=2;manual.toolId="marker";manual.color.profileId=previous.spec.colorSpec.profileId;manual.style.width=24;
    manual.startUs=previous.eventIndex->back().endUs+100;manual.endUs=manual.startUs+100;
    manual.geometry={{232,140,1,{},{},manual.startUs,0,0},{308,140,1,{},{},manual.endUs,0,0}};
    const auto dirty=strokeBounds(manual);CHECK(dirty);CHECK(document.value().apply(Command{Paint{manual,dirty.value()}},previous.revision));
    const auto before=document.value().snapshot();
    auto request=jsonResource({{"type",ReconstructionRequestType},{"mode","refine"},{"image",qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")},
        {"object",QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(Id::fromNumber(15).bytes.data()),16).toHex())},
        {"x",230},{"y",120},{"width",80},{"height",40}});
    auto resources=std::make_shared<ResourceMap>(*before.resources);resources->emplace(request.id,request);auto job=before;job.resources=resources;
    const auto refined=runReconstructionJob(job);if(!refined)throw std::runtime_error(refined.error().messageKey);
    const auto after=decodeDelta(refined.value(),job);CHECK(after);
    CHECK(after.value().eventIndex->size()>before.eventIndex->size());
    for(std::size_t i=before.eventIndex->size();i<after.value().eventIndex->size();++i){
        const auto& e=(*after.value().eventIndex)[i];CHECK(e.objectId==Id::fromNumber(15));CHECK(after.value().nodes->at(e.layerId).stage=="detail");
    }
    const auto command=reconstructionCommand(before,after.value());CHECK(command);CHECK(document.value().apply(command.value(),before.revision));
    const auto a=renderPaintedDocument(before,{before.revision});CHECK(a);
    const auto b=renderPaintedDocument(document.value().snapshot(),{document.value().snapshot().revision});CHECK(b);
    std::size_t changed=0,repaintedOpaque=0;
    for(int y=134;y<146;++y)for(int x=240;x<300;++x){
        const auto at=std::size_t(y*600+x)*4;
        if(a.value().merged.bytes[at+3]==255 && a.value().merged.bytes[at]==0 && a.value().merged.bytes[at+1]==0 && a.value().merged.bytes[at+2]==0 &&
           (b.value().merged.bytes[at] || b.value().merged.bytes[at+1] || b.value().merged.bytes[at+2]))++repaintedOpaque;
    }
    CHECK(repaintedOpaque>0); // Corrective strokes must appear above existing opaque paint.
    for(int y=0;y<400;++y)for(int x=0;x<600;++x)for(unsigned c=0;c<4;++c){
        const auto at=std::size_t(y*600+x)*4+c;
        if(x<230 || x>=310 || y<120 || y>=160)CHECK(a.value().merged.bytes[at]==b.value().merged.bytes[at]);
        else if(a.value().merged.bytes[at]!=b.value().merged.bytes[at])++changed;
    }
    CHECK(changed>0);
    for(std::size_t i=0;i<before.eventIndex->size();++i)CHECK((*before.eventIndex)[i]==(*document.value().snapshot().eventIndex)[i]);
    QTemporaryDir owned;CHECK(owned.isValid());const auto path=std::filesystem::path(owned.filePath("regional.draw").toStdWString());
    ProjectStore store;CHECK(store.save(path,document.value().snapshot()));const auto reopened=store.recover(path);CHECK(reopened);
    CHECK(reopened.value().snapshot.sameContent(document.value().snapshot()));
    const auto replay=renderPaintedDocument(reopened.value().snapshot,{reopened.value().snapshot.revision});CHECK(replay);
    CHECK(replay.value().merged.bytes==b.value().merged.bytes);
    const auto artifacts=qEnvironmentVariable("DRAWBORAD_RECONSTRUCTION_ARTIFACTS");
    if(!artifacts.isEmpty()){
        CHECK(QDir().mkpath(artifacts));exportManifest(b.value(),QDir(artifacts).filePath("coffee-regional-export.json"));
        PsdWriteRequest psd;psd.path=std::filesystem::path(QDir(artifacts).filePath("coffee-regional-layers.psd").toStdWString());
        psd.size=before.spec.pixelSize;psd.format=before.spec.colorSpec.format;psd.ppi=before.spec.ppi;psd.layers=b.value().layers;psd.merged=b.value().merged;psd.overwrite=true;CHECK(writePsd(psd));
        CHECK(store.save(std::filesystem::path(QDir(artifacts).filePath("coffee-regional-native.draw").toStdWString()),document.value().snapshot()));
    }
    CHECK(document.value().undo());CHECK(document.value().snapshot().sameContent(before));
}
TEST("E07 PSD preserves child visibility independently of a hidden object"){
    auto snapshot=coffeeDocument();auto nodes=std::make_shared<NodeMap>(*snapshot.nodes);
    nodes->at(Id::fromNumber(13)).visible=false;snapshot.nodes=nodes;
    const auto rendered=renderPaintedDocument(snapshot,{snapshot.revision});CHECK(rendered);
    bool hiddenGroup=false;
    for(const auto& layer:rendered.value().layers){
        if(!layer.sectionType)CHECK(layer.visible); // Both native children have own visible=true.
        if(layer.sectionType==2 && layer.name=="Visible cup interior"){CHECK(!layer.visible);hiddenGroup=true;}
    }
    CHECK(hiddenGroup);
}
TEST("E07 duplicate saved reconstruction directives are rejected before compilation"){
    auto input=instruction(coffeeDocument(),qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"));
    auto resources=std::make_shared<ResourceMap>(*input.resources);
    const auto duplicate=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")},{"nonce","saved-directive"},{"outputDirectory","unused-test-path"}});
    resources->emplace(duplicate.id,duplicate);input.resources=resources;
    const auto result=runReconstructionJob(input);CHECK(!result);CHECK(result.error().messageKey=="workflow.ambiguous-job-request");
}
