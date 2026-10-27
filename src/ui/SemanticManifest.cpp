#include "SemanticManifest.h"
#include "codecs/ImageReader.h"
#include "core/Digest.h"
#include "ReviewJob.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>
#include <span>

namespace drawborad {
namespace {
// Restrict files to manifest-owned local paths; never follow arbitrary paths
// from externally-generated or untrusted JSON documents.
bool simpleLocalPath(const QString& name, bool mask) {
    if(name.isEmpty() || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':')) ||
       name.startsWith(QLatin1Char('/')) || name.contains(QLatin1String("..")))return false;
    if(mask){
        if(!name.startsWith(QLatin1String("masks/layer-")) || !name.endsWith(QLatin1String(".u8")))return false;
        for(const QChar c:name.mid(12,name.size()-15))if(!c.isDigit())return false;
        return name.size()>=16;
    }
    return name==QLatin1String("source.png") || name==QLatin1String("source.jpg") || name==QLatin1String("source.jpeg");
}
bool matchesHex(const QString& value,const std::string& hex) {
    return value.size()==64 && value.toLower().toStdString()==hex;
}
Result<Bytes> maskFile(const QDir& directory,const QString& relative,
                       std::size_t pixels,const QString& expectedHash) {
    if(!simpleLocalPath(relative,true))return error(ErrorCode::PathEscape,"semantic.mask-path");
    QFile file(directory.filePath(relative));
    if(QFileInfo(directory.filePath(relative)).isSymLink())return error(ErrorCode::PathEscape,"semantic.mask-symlink");
    if(!file.open(QIODevice::ReadOnly) || file.size()!=qint64(pixels))
        return error(ErrorCode::InvalidInput,"semantic.mask-size");
    const QByteArray contents=file.readAll();
    if(contents.size()!=qint64(pixels))return error(ErrorCode::IoError,"semantic.mask-read");
    Bytes bytes(reinterpret_cast<const std::uint8_t*>(contents.constData()),
                reinterpret_cast<const std::uint8_t*>(contents.constData())+contents.size());
    if(!matchesHex(expectedHash,sha256(bytes).hex()))return error(ErrorCode::DigestMismatch,"semantic.mask-hash");
    for(const auto p:bytes)if(p!=0 && p!=255)return error(ErrorCode::InvalidInput,"semantic.mask-binary");
    return bytes;
}
}
Result<SemanticManifestImport> loadSemanticMaskManifest(const QUrl& manifestUrl) {
    if(!manifestUrl.isLocalFile())return error(ErrorCode::InvalidInput,"semantic.local-only");
    const QFileInfo info(manifestUrl.toLocalFile());
    QFile manifest(info.absoluteFilePath());
    if(!manifest.open(QIODevice::ReadOnly) || manifest.size()<=0 || manifest.size()>256*1024)
        return error(ErrorCode::InvalidInput,"semantic.manifest-size");
    QJsonParseError parseError;
    const auto parsed=QJsonDocument::fromJson(manifest.readAll(),&parseError);
    if(parseError.error!=QJsonParseError::NoError || !parsed.isObject())
        return error(ErrorCode::CorruptData,"semantic.manifest-json");
    const auto root=parsed.object();
    if(root.value("schema").toInt(-1)!=1 || root.value("backend").toString()!=QLatin1String("sam3-image") ||
       root.value("promptOrder").toString()!=QLatin1String("back-to-front"))
        return error(ErrorCode::UnsupportedFeature,"semantic.manifest-schema");
    const auto w=root.value("width").toInteger(-1), h=root.value("height").toInteger(-1);
    if(w<=0 || h<=0 || w>4096 || h>4096 || w*h>4*1024*1024)
        return error(ErrorCode::BudgetExceeded,"semantic.image-size");
    const auto imageName=root.value("image").toString();
    if(!simpleLocalPath(imageName,false))return error(ErrorCode::PathEscape,"semantic.image-path");
    const QDir directory=info.dir();
    const QString path=directory.filePath(imageName);
    QFileInfo imageInfo(path);
    if(!imageInfo.isFile() || imageInfo.isSymLink() || imageInfo.size()<=0 || imageInfo.size()>32*1024*1024)
        return error(ErrorCode::InvalidInput,"semantic.image-file");
    const auto image=decodeImage(std::filesystem::path(path.toStdWString()),{});
    if(!image || !image.value().decodedPixels ||
       image.value().info.orientedSize.width!=std::uint64_t(w) ||
       image.value().info.orientedSize.height!=std::uint64_t(h) ||
       (image.value().info.format!=ImageFormat::Png && image.value().info.format!=ImageFormat::Jpeg) ||
       !matchesHex(root.value("sourceSha256").toString(),image.value().info.sourceHash.hex()))
        return error(ErrorCode::DigestMismatch,"semantic.source-verification");
    const auto entries=root.value("objects").toArray();
    const auto pixelCount=std::size_t(w*h);
    if(entries.isEmpty() || entries.size()>64 || std::size_t(entries.size())*pixelCount>56*1024*1024)
        return error(ErrorCode::BudgetExceeded,"semantic.layer-count");
    SemanticManifestImport result;
    result.backend=root.value("backend").toString();
    result.masks.dimensions={std::uint64_t(w),std::uint64_t(h)};
    result.proposals.sourceSize=result.masks.dimensions;
    result.sourceReference=QJsonObject{{"type",SourceReferenceType},
        {"image",QFileInfo(path).absoluteFilePath()},
        {"sourceHash",QString::fromStdString(image.value().info.sourceHash.hex())},
        {"width",w},{"height",h},{"modelBackend",result.backend}};
    Bytes occupied(pixelCount,0);
    int backgrounds=0;
    for(const auto& entry:entries){
        if(!entry.isObject())return error(ErrorCode::InvalidInput,"semantic.object-format");
        const auto object=entry.toObject();
        const auto name=object.value("name").toString();
        if(name.isEmpty() || name.size()>128)return error(ErrorCode::InvalidInput,"semantic.object-name");
        const auto score=object.value("score").toDouble(-1);
        if(!std::isfinite(score) || score<0 || score>1)return error(ErrorCode::InvalidInput,"semantic.object-score");
        if(!object.value("isBackground").isBool())return error(ErrorCode::InvalidInput,"semantic.object-background");
        const bool bg=object.value("isBackground").toBool();
        if(bg && ++backgrounds>1)return error(ErrorCode::InvalidInput,"semantic.multiple-backgrounds");
        if(bg && !result.names.empty())return error(ErrorCode::InvalidInput,"semantic.background-order");
        const auto mask=maskFile(directory,object.value("mask").toString(),pixelCount,object.value("sha256").toString());
        if(!mask)return mask.error();
        std::size_t count=0;
        for(std::size_t i=0;i<pixelCount;++i){
            if(mask.value()[i]){
                if(occupied[i])return error(ErrorCode::InvalidInput,"semantic.overlapping-masks");
                occupied[i]=255;++count;
            }
        }
        if(count==0 || qint64(count)!=object.value("pixels").toInteger(-1))
            return error(ErrorCode::InvalidInput,"semantic.mask-pixels");
        result.masks.fullResolutionMasks.push_back(mask.value());
        const auto index=result.names.size();
        result.proposals.candidates.push_back({Id::fromNumber(index+1),{0,0,std::uint64_t(w),std::uint64_t(h)},{},score});
        result.names.push_back(name);result.scores.push_back(score);result.background.push_back(bg);
    }
    // Model-generated layers must partition the *visible* source. Never
    // pretend that a black hole is a valid "unknown background" object.
    const auto& tile=*image.value().decodedPixels;
    const auto channels=tile.format.channelCount();
    if(tile.width!=std::uint64_t(w) || tile.height!=std::uint64_t(h) ||
       tile.format.depth!=Depth::U8 || tile.format.mode!=ColorMode::RGB)
        return error(ErrorCode::UnsupportedFeature,"semantic.source-layout");
    for(std::int64_t y=0;y<h;++y)for(std::int64_t x=0;x<w;++x){
        const auto i=std::size_t(y*w+x);
        const auto expected=tile.format.alpha?tile.bytes[std::size_t(y*tile.rowStride+x*channels+3)]>0:true;
        if((occupied[i]!=0)!=expected)return error(ErrorCode::InvalidInput,"semantic.source-coverage");
    }
    return result;
}
} // namespace drawborad