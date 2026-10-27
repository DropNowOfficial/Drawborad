#include "Test.h"
#include "ui/SemanticManifest.h"
#include "codecs/ImageReader.h"
#include "core/Digest.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

using namespace drawborad;
namespace {
void write(const QString& p,const QByteArray& data){
    QFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("fixture-write");
}
QString digest(const QByteArray& data){
    const Bytes b(reinterpret_cast<const std::uint8_t*>(data.constData()),
                  reinterpret_cast<const std::uint8_t*>(data.constData())+data.size());
    return QString::fromStdString(sha256(b).hex());
}
QJsonObject makeFixture(const QString& folder) {
    QDir dir(folder);CHECK(dir.mkpath("masks"));
    QImage img(4,3,QImage::Format_RGBA8888);img.fill(qRgba(20,50,100,255));
    CHECK(img.save(dir.filePath("source.png")));
    const auto image=decodeImage(std::filesystem::path(dir.filePath("source.png").toStdWString()),{});
    CHECK(image);
    QByteArray background(12,char(255));background[5]=0;
    QByteArray cup(12,char(0));cup[5]=char(255);
    write(dir.filePath("masks/layer-00.u8"),background);
    write(dir.filePath("masks/layer-01.u8"),cup);
    auto entry=[&](const QString& n,const QByteArray& data,int index,bool bg,int pixels){
        return QJsonObject{{"name",n},{"score",0.91},{"isBackground",bg},
                           {"pixels",pixels},{"mask",QString("masks/layer-%1.u8").arg(index,2,10,QLatin1Char('0'))},
                           {"sha256",digest(data)}};
    };
    QJsonObject root{{"schema",1},{"backend","sam3-image"},{"promptOrder","back-to-front"},
                     {"image","source.png"},{"sourceSha256",QString::fromStdString(image.value().info.sourceHash.hex())},
                     {"width",4},{"height",3},
                     {"objects",QJsonArray{entry("Background (unrecognized)",background,0,true,11),entry("cup",cup,1,false,1)}}};
    write(dir.filePath("manifest.json"),QJsonDocument(root).toJson());
    return root;
}
}
TEST("U04 semantic model manifest imports independent cup and background masks") {
    QTemporaryDir dir;CHECK(dir.isValid());auto doc=makeFixture(dir.path());
    const auto r=loadSemanticMaskManifest(QUrl::fromLocalFile(QDir(dir.path()).filePath("manifest.json")));
    CHECK(r);CHECK(r.value().names.size()==2);CHECK(r.value().names[1]==QStringLiteral("cup"));
    CHECK(r.value().background[0]);CHECK(!r.value().background[1]);
    CHECK(r.value().masks.fullResolutionMasks[0][5]==0);
    CHECK(r.value().masks.fullResolutionMasks[1][5]==255);
    CHECK(r.value().sourceReference.value("sourceHash")==doc.value("sourceSha256"));
}
TEST("U04 semantic model manifest rejects mask hash mismatch") {
    QTemporaryDir dir;CHECK(dir.isValid());makeFixture(dir.path());
    QByteArray cup(12,char(255));write(QDir(dir.path()).filePath("masks/layer-01.u8"),cup);
    const auto r=loadSemanticMaskManifest(QUrl::fromLocalFile(QDir(dir.path()).filePath("manifest.json")));
    CHECK(!r);CHECK(r.error().code==ErrorCode::DigestMismatch);
}
TEST("U04 semantic model manifest refuses path traversal") {
    QTemporaryDir dir;CHECK(dir.isValid());auto manifest=makeFixture(dir.path());
    auto objects=manifest.value("objects").toArray();auto cup=objects[1].toObject();cup["mask"]="../outside.u8";objects[1]=cup;manifest["objects"]=objects;
    write(QDir(dir.path()).filePath("manifest.json"),QJsonDocument(manifest).toJson());
    const auto r=loadSemanticMaskManifest(QUrl::fromLocalFile(QDir(dir.path()).filePath("manifest.json")));
    CHECK(!r);CHECK(r.error().code==ErrorCode::PathEscape);
}
TEST("U04 semantic model manifest rejects overlapping object files") {
    QTemporaryDir dir;CHECK(dir.isValid());auto manifest=makeFixture(dir.path());
    QByteArray cup(12,char(0));cup[5]=char(255);cup[6]=char(255);
    write(QDir(dir.path()).filePath("masks/layer-01.u8"),cup);
    auto objects=manifest.value("objects").toArray();auto o=objects[1].toObject();o["sha256"]=digest(cup);o["pixels"]=2;objects[1]=o;manifest["objects"]=objects;
    write(QDir(dir.path()).filePath("manifest.json"),QJsonDocument(manifest).toJson());
    const auto r=loadSemanticMaskManifest(QUrl::fromLocalFile(QDir(dir.path()).filePath("manifest.json")));
    CHECK(!r);CHECK(r.error().messageKey=="semantic.overlapping-masks");
}