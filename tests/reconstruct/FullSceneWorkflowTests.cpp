#include "Test.h"
#include "ui/ReviewJob.h"
#include "review/ReviewSession.h"
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include "codecs/ImageReader.h"
#include "storage/ProjectCodec.h"
#include "storage/ProjectStore.h"
#include "ui/ReconstructionJob.h"
#include "ui/PaintedDocument.h"
#include "codecs/RasterWriters.h"
#include <QTemporaryDir>
using namespace drawborad;
TEST("E08 optional missing guides never abort ordinary plus thin accepted objects"){
    QTemporaryDir owned;CHECK(owned.isValid());
    for(unsigned shape=0;shape<3;++shape){
        NewDocumentRequest request;request.dimensions={32,32,Unit::Px,300};const auto spec=validateDocument(request,{});CHECK(spec);
        auto document=Document::create(spec.value(),Id::fromNumber(4100+shape));CHECK(document);
        TileData pixels;pixels.width=32;pixels.height=32;pixels.format=spec.value().colorSpec.format;pixels.profileId=spec.value().colorSpec.profileId;pixels.rowStride=128;pixels.bytes.assign(4096,255);
        for(unsigned p=0;p<1024;++p){pixels.bytes[p*4]=80;pixels.bytes[p*4+1]=120;pixels.bytes[p*4+2]=160;}
        const auto encoded=encodeRaster(ExportFormat::Png,pixels,{});CHECK(encoded);const auto path=owned.filePath(QString("thin-%1.png").arg(shape));
        QFile file(path);CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(reinterpret_cast<const char*>(encoded.value().data()),qint64(encoded.value().size()))==qint64(encoded.value().size()));file.close();
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",path},{"sourceHash",QString::fromStdString(sha256(encoded.value()).hex())},{"width",32},{"height",32}});
        Bytes thin(1024,0);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)
            if(shape==0?x==4:shape==1?(x==4 || x==5):(x>=4 && x<7 && y>=4 && y<7))thin[y*32+x]=255;
        Bytes ordinary(1024);for(unsigned p=0;p<1024;++p)ordinary[p]=255-thin[p];
        Command setup;setup.operations.emplace_back(InstallResource{source});
        for(unsigned i=0;i<2;++i){
            const auto& bytes=i?thin:ordinary;Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{source.id}};
            LayerNode object;object.id=Id::fromNumber(4200+i);object.kind=NodeKind::Object;object.parent=Document::rootId();object.maskId=mask.id;object.name=i?"Thin accepted object":"Ordinary accepted object";object.bounds={0,0,32,32};
            LayerNode layer;layer.id=Id::fromNumber(4300+i);layer.parent=object.id;layer.objectId=object.id;layer.name="Paint";layer.bounds=object.bounds;
            setup.operations.emplace_back(InstallResource{mask});setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{layer});
        }
        CHECK(document.value().apply(setup,0));const auto before=document.value().snapshot();auto input=before;auto resources=std::make_shared<ResourceMap>(*input.resources);
        const auto instruction=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",path}});resources->emplace(instruction.id,instruction);input.resources=resources;
        const auto result=runReconstructionJob(input);if(!result)throw std::runtime_error(result.error().messageKey);
        const auto output=decodeDelta(result.value(),input);CHECK(output);CHECK(std::any_of(output.value().eventIndex->begin(),output.value().eventIndex->end(),[](const auto& e){return e.objectId==Id::fromNumber(4201) && e.toolId=="marker";}));
        const auto painted=renderPaintedDocument(output.value(),{output.value().revision});CHECK(painted);
        for(unsigned p=0;p<1024;++p)CHECK(painted.value().merged.bytes[p*4+3]==255);
        CHECK(before.eventIndex->empty());
    }
}
TEST("E08 prompted review validates actual point box coordinates and explicit mode"){
    const auto parsed=reviewPromptRequest({{"proposalMode","prompted"},{"points",QJsonArray{QJsonObject{{"x",280},{"y",225},{"positive",true}},QJsonObject{{"x",414},{"y",94},{"positive",false}}}},
        {"box",QJsonArray{170,12,425,316}}},{600,400});
    CHECK(parsed);CHECK(parsed.value());CHECK(parsed.value()->points.size()==2);CHECK(!parsed.value()->points[1].positive);CHECK(parsed.value()->boxes.size()==1);
    CHECK(!reviewPromptRequest({{"proposalMode","prompted"}},{600,400}));
    CHECK(!reviewPromptRequest({{"proposalMode","prompted"},{"points",QJsonArray{QJsonObject{{"x",601},{"y",1},{"positive",true}}}}},{600,400}));
    CHECK(!reviewPromptRequest({{"proposalMode","prompted"},{"box",QJsonArray{10,10,5,20}}},{600,400}));
    const auto automatic=reviewPromptRequest({}, {600,400});CHECK(automatic);CHECK(!automatic.value());
}
TEST("E08 adding real proposal masks preserves edits selection geometry and undo"){
    ProposalSet p;p.sourceSize={8,8};p.candidates.push_back({Id::fromNumber(1),{0,0,8,8},{},.9});
    MaskSet m;m.dimensions={8,8};m.fullResolutionMasks={Bytes(64,0)};m.fullResolutionMasks[0][10]=255;
    auto review=ReviewSession::create(p,m,3);CHECK(review);
    auto more=p;more.candidates[0].id=Id::fromNumber(2);auto masks=m;masks.fullResolutionMasks[0][20]=255;
    CHECK(review.value().appendProposals(more,masks,3));CHECK(review.value().masks().fullResolutionMasks.size()==2);
    CHECK(review.value().masks().fullResolutionMasks[0]==m.fullResolutionMasks[0]);CHECK(review.value().masks().fullResolutionMasks[1][20]==255);
    CHECK(!review.value().appendProposals(more,masks,3));CHECK(review.value().undo());CHECK(review.value().masks().fullResolutionMasks==m.fullResolutionMasks);
    CHECK(review.value().appendProposals(more,masks,review.value().revision(),true));CHECK(review.value().masks().fullResolutionMasks[0][20]==255);
}
TEST("E08 visible-object subtraction removes overlap without inventing occluded pixels"){
    ProposalSet p;p.sourceSize={8,8};p.candidates={{Id::fromNumber(1),{0,0,8,8},{},.9},{Id::fromNumber(2),{0,0,8,8},{},.8}};
    MaskSet m;m.dimensions={8,8};m.fullResolutionMasks={Bytes(64,255),Bytes(64,0)};m.fullResolutionMasks[1][27]=255;
    auto review=ReviewSession::create(p,m,0);CHECK(review);MaskEdit edit;edit.operation=MaskEditOp::SubtractMask;edit.objectIndex=0;edit.secondaryIndex=1;
    CHECK(review.value().apply(edit));CHECK(review.value().masks().fullResolutionMasks[0][27]==0);CHECK(review.value().masks().fullResolutionMasks[1]==m.fullResolutionMasks[1]);
    CHECK(review.value().undo());CHECK(review.value().masks().fullResolutionMasks==m.fullResolutionMasks);
}
TEST("E08 explicit remaining-background proposal preserves source transparency and selected foreground holes"){
    ProposalSet p;p.sourceSize={8,8};p.candidates={{Id::fromNumber(1),{0,0,8,8},{},.9}};
    MaskSet m;m.dimensions={8,8};m.fullResolutionMasks={Bytes(64,0)};m.fullResolutionMasks[0][27]=255;
    auto review=ReviewSession::create(p,m,0);CHECK(review);Bytes alpha(64,255);for(unsigned i=56;i<64;++i)alpha[i]=0;
    const std::array<std::size_t,1> selected{0};CHECK(review.value().addBackground(selected,alpha,0));
    CHECK(review.value().masks().fullResolutionMasks.size()==2);const auto& background=review.value().masks().fullResolutionMasks[0];
    for(unsigned i=0;i<64;++i)CHECK(background[i]==(i==27?0:alpha[i]));
    CHECK(review.value().masks().fullResolutionMasks[1]==m.fullResolutionMasks[0]);
    CHECK(review.value().undo());CHECK(review.value().masks().fullResolutionMasks==m.fullResolutionMasks);
}
#ifdef DRAWBORAD_FULL_SCENE_REAL_REFERENCE
TEST("E08 reviewed actual full coffee scene paints every region from blank and reopens without source"){
    const auto directory=QDir(qEnvironmentVariable("DRAWBORAD_FULL_SCENE_REFERENCE")).absoluteFilePath("../reviewed-scene");
    QFile review(QDir(directory).filePath("review.json"));CHECK(review.open(QIODevice::ReadOnly));const auto manifest=QJsonDocument::fromJson(review.readAll()).object();
    CHECK(manifest.value("selected_union_pixels").toInteger()==240000);CHECK(manifest.value("overlap_pixels").toInteger()==0);
    QTemporaryDir ownSource;CHECK(ownSource.isValid());const auto sourcePath=ownSource.filePath("coffee-source.png");CHECK(QFile::copy(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"),sourcePath));
    const auto decoded=decodeImage(std::filesystem::path(sourcePath.toStdWString()),{});CHECK(decoded);CHECK(decoded.value().info.sourceHash.hex()==manifest.value("source_sha256").toString().toStdString());
    NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};const auto spec=validateDocument(request,{});CHECK(spec);auto document=Document::create(spec.value(),Id::fromNumber(1800));CHECK(document);
    auto source=jsonResource({{"type",SourceReferenceType},{"image",sourcePath},{"sourceHash",manifest.value("source_sha256")},{"width",600},{"height",400}});
    Command setup;setup.operations.emplace_back(InstallResource{source});std::vector<Bytes> masks;unsigned ordinal=0;
    for(const auto& value:manifest.value("regions").toArray()){
        const auto item=value.toObject();QFile file(QDir(directory).filePath(item.value("mask").toString()));CHECK(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        masks.emplace_back(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());CHECK(masks.back().size()==240000);
        Resource mask{sha256(masks.back()),ResourceKind::Mask,std::make_shared<const Bytes>(masks.back()),{source.id}};CHECK(mask.id.hex()==item.value("sha256").toString().toStdString());
        LayerNode object;object.id=Id::fromNumber(1905-ordinal);object.parent=Document::rootId();object.kind=NodeKind::Object;object.name=item.value("name").toString().toStdString();object.maskId=mask.id;object.bounds={0,0,600,400};
        LayerNode layer;layer.id=Id::fromNumber(2000+ordinal++);layer.parent=object.id;layer.objectId=object.id;layer.name="Editable paint";layer.bounds=object.bounds;
        setup.operations.emplace_back(InstallResource{mask});setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{layer});
    }
    CHECK(document.value().apply(setup,0));const auto before=document.value().snapshot();CHECK(before.eventIndex->empty());
    auto input=before;auto resources=std::make_shared<ResourceMap>(*input.resources);const auto instruction=jsonResource({{"type",ReconstructionRequestType},{"mode","reconstruct"},{"image",sourcePath}});resources->emplace(instruction.id,instruction);input.resources=resources;
    const auto delta=runReconstructionJob(input);if(!delta)throw std::runtime_error(delta.error().messageKey);const auto output=decodeDelta(delta.value(),input);CHECK(output);
    const auto command=reconstructionCommand(before,output.value());CHECK(command);CHECK(document.value().apply(command.value(),before.revision));const auto paintedSnapshot=document.value().snapshot();
    const auto painted=renderPaintedDocument(paintedSnapshot,{paintedSnapshot.revision});CHECK(painted);CHECK(painted.value().statistics.sourceAssetReads==0);
    QTemporaryDir transientOutputs;CHECK(transientOutputs.isValid());
    const auto artifacts=qEnvironmentVariable("DRAWBORAD_FULL_SCENE_ARTIFACTS",transientOutputs.path());CHECK(QDir().mkpath(artifacts));
    auto write=[&](const QString& name,const Bytes& data){QFile file(QDir(artifacts).filePath(name));CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(reinterpret_cast<const char*>(data.data()),qint64(data.size()))==qint64(data.size()));};
    const auto png=encodeRaster(ExportFormat::Png,painted.value().merged,{});CHECK(png);write("coffee-full-strokes.png",png.value());
    const auto blank=renderPaintedDocument(paintedSnapshot,{paintedSnapshot.revision,0});CHECK(blank);CHECK(std::all_of(blank.value().merged.bytes.begin(),blank.value().merged.bytes.end(),[](auto v){return v==0;}));
    const auto early=renderPaintedDocument(paintedSnapshot,{paintedSnapshot.revision,std::max<std::size_t>(1,paintedSnapshot.eventIndex->size()/4)});CHECK(early);
    const auto earlyPng=encodeRaster(ExportFormat::Png,early.value().merged,{});CHECK(earlyPng);write("coffee-quarter-replay.png",earlyPng.value());
    QJsonArray layers;for(const auto& layer:painted.value().layers)layers.append(QJsonObject{{"name",QString::fromStdString(layer.name)},{"pixelsSha256",QString::fromStdString(sha256(layer.pixels.bytes).hex())},{"maskSha256",QString::fromStdString(sha256(layer.mask).hex())},{"opacity",layer.opacity},{"visible",layer.visible},{"sectionType",layer.sectionType?int(*layer.sectionType):-1}});
    const auto exportManifest=QJsonDocument(QJsonObject{{"width",600},{"height",400},{"mergedSha256",QString::fromStdString(sha256(painted.value().merged.bytes).hex())},{"layers",layers}}).toJson();
    write("coffee-full-export.json",Bytes(exportManifest.begin(),exportManifest.end()));write("coffee-full-export.json.rgba",painted.value().merged.bytes);
    PsdWriteRequest psd;psd.size={600,400};psd.format=before.spec.colorSpec.format;psd.ppi=before.spec.ppi;psd.layers=painted.value().layers;psd.merged=painted.value().merged;const auto encoded=encodePsd(psd);CHECK(encoded);write("coffee-full-layers.psd",encoded.value());
    ProjectStore store;const auto path=std::filesystem::path(QDir(artifacts).filePath("coffee-full-native.draw").toStdWString());CHECK(store.save(path,paintedSnapshot));CHECK(QFile::remove(sourcePath));
    const auto reopened=store.recover(path);CHECK(reopened);CHECK(reopened.value().snapshot.sameContent(paintedSnapshot));const auto replay=renderPaintedDocument(reopened.value().snapshot,{reopened.value().snapshot.revision});CHECK(replay);CHECK(replay.value().merged.bytes==painted.value().merged.bytes);
    std::uint64_t covered=0,opaque=0;double color=0,alpha=0;const auto& reference=*decoded.value().decodedPixels;
    for(unsigned p=0;p<240000;++p){const auto a=painted.value().merged.bytes[p*4+3];covered+=a>0;opaque+=a==255;alpha+=1-a/255.;for(unsigned c=0;c<3;++c)color+=std::abs(int(reference.bytes[(p/600)*reference.rowStride+(p%600)*reference.format.channelCount()+c])-int(painted.value().merged.bytes[p*4+c]))/255./3.;}
    const auto report=QJsonDocument(QJsonObject{{"selectedPixels",240000},{"paintedPixels",qint64(covered)},{"opaquePixels",qint64(opaque)},{"rgbMae",color/240000},{"alphaMae",alpha/240000},{"events",qint64(paintedSnapshot.eventIndex->size())},{"sourceAssetReads",0},{"wholeImageFidelityVerified",false},{"naturalProcessReviewed",false},{"savedReopenedAfterSourceRemoval",true}}).toJson();write("actual-full-scene-report.json",Bytes(report.begin(),report.end()));
    std::cout<<report.constData();CHECK(covered==240000);CHECK(opaque==240000);
    CHECK(paintedSnapshot.eventIndex->front().objectId==before.nodes->at(Document::rootId()).children.front());
}
TEST("E08 real point box native masks match the separately measured official scene reference"){
    const auto model=validatePackage(std::filesystem::path(qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE").toStdWString()));CHECK(model);
    const auto image=decodeImage(std::filesystem::path(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE").toStdWString()),{});CHECK(image);CHECK(image.value().decodedPixels);
    QFile file(QDir(qEnvironmentVariable("DRAWBORAD_FULL_SCENE_REFERENCE")).filePath("report.json"));CHECK(file.open(QIODevice::ReadOnly));
    const auto report=QJsonDocument::fromJson(file.readAll()).object();CHECK(report.value("socket_connect_attempts").toInteger()==0);
    CHECK(report.value("source_sha256").toString().toStdString()==image.value().info.sourceHash.hex());
    ProposalEngine engine;
    for(const auto& value:report.value("cases").toArray()){
        const auto item=value.toObject();const auto name=item.value("name").toString();if(!name.endsWith("reviewed-points"))continue;
        QJsonArray points;for(const auto& raw:item.value("points").toArray()){const auto p=raw.toArray();points.append(QJsonObject{{"x",p[0]},{"y",p[1]},{"positive",p[2].toInteger()==1}});}
        const QJsonObject metadata{{"proposalMode","prompted"},{"points",points},{"box",item.value("box")}};
        const auto prompts=reviewPromptRequest(metadata,{600,400});CHECK(prompts);CHECK(prompts.value());
        AnalysisRequest request;request.sourceSize={600,400};request.model=model.value();request.view.rgbTiles={*image.value().decodedPixels};request.prompts=*prompts.value();
        const auto proposals=engine.propose(request);CHECK(proposals);const auto masks=projectMasks(proposals.value(),{{600,400},true});CHECK(masks);CHECK(masks.value().fullResolutionMasks.size()==3);
        for(unsigned i=0;i<3;++i){
            const auto expected=decodeImage(std::filesystem::path(QDir(qEnvironmentVariable("DRAWBORAD_FULL_SCENE_REFERENCE")).filePath(name+QString("-%1.png").arg(i)).toStdWString()),{});CHECK(expected);CHECK(expected.value().decodedPixels);
            const auto& e=*expected.value().decodedPixels;std::uint64_t both=0,either=0;
            for(std::size_t p=0;p<240000;++p){const bool a=masks.value().fullResolutionMasks[i][p]!=0,b=e.bytes[(p/600)*e.rowStride+(p%600)*e.format.channelCount()]!=0;both+=a&&b;either+=a||b;}
            const auto iou=either?double(both)/double(either):1;std::cout<<name.toStdString()<<" mask="<<i<<" native/official IoU="<<iou<<'\n';CHECK(iou>=.995);
            CHECK(std::abs(proposals.value().candidates[i].score-item.value("scores").toArray()[i].toDouble())<.001);
        }
        if(name=="cup-reviewed-points"){
            NewDocumentRequest newRequest;newRequest.dimensions={1,1,Unit::Px,300};const auto spec=validateDocument(newRequest,{});CHECK(spec);auto document=Document::create(spec.value(),Id::fromNumber(500));CHECK(document);
            auto instruction=metadata;instruction.insert("type",ReviewRequestType);instruction.insert("image",qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"));instruction.insert("package",qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE"));instruction.insert("expectedSourceHash",QString::fromStdString(image.value().info.sourceHash.hex()));
            auto resource=jsonResource(instruction);auto snapshot=document.value().snapshot();auto resources=std::make_shared<ResourceMap>(*snapshot.resources);resources->emplace(resource.id,resource);snapshot.resources=resources;
            const auto delta=runImageReviewJob(snapshot,{});CHECK(delta);const auto output=decodeDelta(delta.value(),snapshot);CHECK(output);const auto result=findJsonResource(output.value(),ReviewResultType);CHECK(result);CHECK(result->value("proposalMode").toString()=="prompted");CHECK(result->value("candidates").toArray().size()==3);
        }
    }
}
#endif
