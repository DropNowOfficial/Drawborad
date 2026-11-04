#include "Test.h"
#include "headless/HeadlessWorkflow.h"
#include "codecs/ImageReader.h"
#include "ui/ReviewJob.h"
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
using namespace drawborad;
namespace {
SemanticManifestImport fixture(const QString& path) {
    QImage image(64,64,QImage::Format_RGBA8888);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        image.setPixelColor(x,y,x<32?QColor(180,60,40):QColor(40,100,180));
    CHECK(image.save(path));
    auto decoded=decodeImage(std::filesystem::path(path.toStdString()),{});CHECK(decoded);
    SemanticManifestImport input;input.backend="fixture-no-ai";
    input.masks.dimensions={64,64};input.proposals.sourceSize={64,64};
    input.sourceReference={{"type",SourceReferenceType},{"image",path},{"width",64},{"height",64},
        {"sourceHash",QString::fromStdString(decoded.value().info.sourceHash.hex())},{"modelBackend",input.backend}};
    for(unsigned i=0;i<2;++i){
        Bytes mask(4096);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)mask[y*64+x]=((x<32)==(i==0))?255:0;
        input.masks.fullResolutionMasks.push_back(mask);
        input.proposals.candidates.push_back({Id::fromNumber(i+1),{0,0,64,64},{},1});
        input.names.push_back(i==0?"Left":"Right");input.scores.push_back(1);input.background.push_back(false);
    }
    return input;
}
}
TEST("H01 accepted objects generate real independent C++ strokes") {
    QTemporaryDir temp;CHECK(temp.isValid());
    auto input=fixture(temp.filePath("source.png"));
    auto result=runHeadlessWorkflow({input,{0,1},std::filesystem::path(temp.filePath("result").toStdString())});
    CHECK(result);
    const auto& snapshot=result.value().snapshot;
    CHECK(!snapshot.eventIndex->empty());
    std::size_t objects=0;
    for(const auto& [id,node]:*snapshot.nodes){
        (void)id;if(node.kind==NodeKind::Object)++objects;
        CHECK(node.sourceResource.empty());CHECK(node.content.empty());
    }
    CHECK(objects==2);
    for(const auto& event:*snapshot.eventIndex){
        CHECK(!event.geometry.empty());CHECK(event.endUs>=event.startUs);
        CHECK(snapshot.nodes->at(event.layerId).objectId==event.objectId);
    }
    CHECK(result.value().metadata.value("backend").toString()=="fixture-no-ai");
    CHECK(!result.value().metadata.value("wholeImageFidelityVerified").toBool());
}
TEST("H01 acceptance and cancellation are explicit and atomic") {
    QTemporaryDir temp;CHECK(temp.isValid());auto input=fixture(temp.filePath("source.png"));
    const auto output=std::filesystem::path(temp.filePath("result").toStdString());
    CHECK(!runHeadlessWorkflow({input,{},output}));
    CHECK(!runHeadlessWorkflow({input,{0,0},output}));
    CHECK(!runHeadlessWorkflow({input,{2},output}));
    std::stop_source cancel;cancel.request_stop();
    auto result=runHeadlessWorkflow({input,{0,1},output},cancel.get_token());
    CHECK(!result);CHECK(result.error().code==ErrorCode::Cancelled);
    CHECK(!std::filesystem::exists(output/"report.json"));
    input.masks.fullResolutionMasks[1]=input.masks.fullResolutionMasks[0];
    CHECK(!runHeadlessWorkflow({input,{0,1},output}));
}
