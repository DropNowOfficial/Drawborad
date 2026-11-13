#include "platform/QtSolidRasterCommand.h"
#include "core/MyPaintCroppedRaster.h"
#include "storage/ProjectStore.h"
#include "ui/LayerStream.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "storage/Binary.h"
using namespace drawborad;
static Document document(unsigned width,unsigned height) {
    NewDocumentRequest req;req.dimensions={double(width),double(height),Unit::Px,300};
    req.transparent=false;req.paperColor.channels={1,1,1,0};
    auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(91));assert(doc);return std::move(doc.value());
}
static QtSolidMaskCache::Shape shape(double scale=1) {
    QtSolidMaskCache::Shape out{1,{QPointF(50,40),QPointF(950,20),QPointF(920,900),QPointF(60,950),QPointF(250,700),QPointF(10,300)}};
    for(auto& p:out.controls)p*=scale;
    return out;
}
int main() {
    auto doc=document(1024,1024);ColorValue blue;blue.channels={0,0,1,0};
    auto first=buildQtSolidRasterCommand(doc.snapshot(),0,shape(.1),blue,1,Id::fromNumber(2));assert(first);
    assert(doc.apply(first.value().command,first.value().baseRevision));
    const auto before=doc.snapshot();ColorValue red;red.channels={1,.1,.05,0};red.alpha=.7;
    auto candidate=buildQtSolidRasterCommand(before,before.revision,shape(),red,.8,Id::fromNumber(3));
    if(!candidate){std::cerr<<candidate.error().messageKey<<'\n';return 1;}
    assert(doc.snapshot().sameContent(before));
    assert(candidate.value().bounds.width>512 && candidate.value().bounds.height>512);
    assert(doc.apply(candidate.value().command,candidate.value().baseRevision));
    const auto accepted=doc.snapshot();const auto& node=accepted.nodes->at(Id::fromNumber(3));
    assert(node.name=="Qt solid (baked pixels)" && node.stage==croppedRaster::NormalStage && node.locked);
    assert(accepted.eventIndex->empty());
    for(const auto& [id,n]:*before.nodes)if(id!=Document::rootId())assert(accepted.nodes->at(id)==n);
    for(const auto& [id,r]:*before.resources)assert(accepted.resources->at(id)==r);
    auto view=croppedRaster::parse(accepted,Id::fromNumber(3));assert(view);
    assert(sha256(view.value().rgba)==candidate.value().pixelHash);
    const auto& bytes=*accepted.resources->at(view.value().manifest).bytes;
    const auto metadata=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),int(bytes.size()))).object();
    assert(metadata["representation"].toString()=="baked-not-editable");
    assert(metadata["producer"].toString()=="Drawborad Qt CPU solid bake");
    assert(!metadata["resampled"].toBool(true));
    assert(doc.undo() && doc.snapshot().sameContent(before));assert(doc.redo() && doc.snapshot().sameContent(accepted));
    // Real project save/recovery; no new persistent event version is introduced.
    QTemporaryDir dir;assert(dir.isValid());ProjectStore store;
    const auto file=(dir.path()+"/baked.draw").toStdString();assert(store.save(file,doc.snapshot()));
    auto reopened=store.recover(file);assert(reopened&&reopened.value().snapshot.sameContent(accepted));
    auto restored=Document::fromSnapshot(reopened.value().snapshot);assert(restored);
    auto stream=NativeLayerStream::create(restored.value().snapshot(),{restored.value().snapshot().revision});assert(stream);
    bool located=false;
    for(std::size_t i=0;i<stream.value().size();++i) {
        auto layer=stream.value().read(i);assert(layer);
        if(layer.value().name==node.name && layer.value().x==view.value().bounds.x && layer.value().y==view.value().bounds.y && layer.value().pixels.width==view.value().bounds.width) {
            located=true;assert(layer.value().pixels.bytes==Bytes(view.value().rgba.begin(),view.value().rgba.end()));
        }
    }
    assert(located);
    auto merged=renderMergedDocument(accepted,{accepted.revision});assert(merged);
    const auto& rgba=merged.value().merged;
    assert(rgba.width==1024&&rgba.height==1024&&rgba.bytes[0]==255&&rgba.bytes[1]==255&&rgba.bytes[2]==255&&rgba.bytes[3]==255);
    const auto center=(500*rgba.rowStride+400*4);assert(rgba.bytes[center]>rgba.bytes[center+2]&&rgba.bytes[center+3]==255);
    // Stale acceptance cannot apply the same prepared command twice.
    assert(!doc.apply(candidate.value().command,candidate.value().baseRevision));
    std::stop_source stop;stop.request_stop();
    auto cancelled=buildQtSolidRasterCommand(before,before.revision,shape(),red,.8,Id::fromNumber(4),stop.get_token());
    assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
    assert(!buildQtSolidRasterCommand(before,before.revision+1,shape(),red,1,Id::fromNumber(4)));
    assert(!buildQtSolidRasterCommand(before,before.revision,shape(),red,1,Id::fromNumber(2)));
    assert(!buildQtSolidRasterCommand(before,before.revision,shape(),red,0,Id::fromNumber(4)));
    auto invalid=shape();invalid.controls[0].setX(NAN);assert(!buildQtSolidRasterCommand(before,before.revision,invalid,red,1,Id::fromNumber(4)));
    auto outside=shape();for(auto& p:outside.controls)p+=QPointF(2000,2000);
    assert(!buildQtSolidRasterCommand(before,before.revision,outside,red,1,Id::fromNumber(4)));
    auto profiled=before;profiled.spec.colorSpec.profileId.bytes[0]=1; // fail without conversion
    assert(!buildQtSolidRasterCommand(profiled,profiled.revision,shape(),red,1,Id::fromNumber(4)));
    auto large=document(3300,5100);auto huge=shape();
    for(auto& p:huge.controls)p=QPointF(p.x()*3.4,p.y()*5.3);
    auto refused=buildQtSolidRasterCommand(large.snapshot(),0,huge,red,1,Id::fromNumber(2));
    assert(!refused && refused.error().code==ErrorCode::BudgetExceeded);
    assert(refused.error().messageKey=="solid-bake.native-roi-too-large");
    assert(doc.snapshot().sameContent(accepted) && before.resources->size()>0);
    if(const char* proof=std::getenv("DRAWBORAD_SOLID_BAKE_PROOF")) {
        std::filesystem::create_directories(proof);const auto base=std::filesystem::path(proof);
        assert(store.save(base/"baked.draw",accepted));
        auto write=[&](const char* name,std::span<const std::uint8_t> data) {
            std::ofstream f(base/name,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));assert(f.good());
        };
        write("merged.rgba",rgba.bytes);write("solid-layer.rgba",view.value().rgba);
        const auto encoded=encodeDelta(DocumentSnapshot{},accepted);assert(encoded);
        binary::Reader reader{encoded.value()};assert(reader.text(32)=="DRAW-DELTA-14");
        std::ofstream details(base/"layout.txt");
        details<<"canvas 1024 1024 RGBA8\nroi "<<view.value().bounds.x<<" "<<view.value().bounds.y<<" "<<view.value().bounds.width<<" "<<view.value().bounds.height<<"\n";
        details<<"merged-sha256 "<<sha256(rgba.bytes).hex()<<"\nlayer-sha256 "<<candidate.value().pixelHash.hex()<<"\nformat DRAW-DELTA-14\n";
    }
    std::cout<<"PASS Qt solid baked native ROI>512; candidate/Undo/save/recover/normal layer stream/merged pixels; original resources preserved; no new format/editable-vector claim\n";
}
