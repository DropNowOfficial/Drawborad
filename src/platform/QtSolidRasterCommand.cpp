#include "QtSolidRasterCommand.h"
#include "core/MyPaintCroppedRaster.h"
#include "codecs/RasterReaders.h"
#include "codecs/RasterWriters.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
namespace drawborad {
Result<QtSolidRasterCandidate> buildQtSolidRasterCommand(const DocumentSnapshot& source,
    Revision expectedRevision,const QtSolidMaskCache::Shape& shape,const ColorValue& color,
    double opacity,NodeId newLayer,std::stop_token stop) try {
    const auto cancelled=[] { return error(ErrorCode::Cancelled,"solid-bake.cancelled"); };
    if(stop.stop_requested())return cancelled();
    if(source.revision!=expectedRevision)return error(ErrorCode::StaleRevision,"solid-bake.revision");
    auto valid=validateSnapshot(source);if(!valid)return valid.error();
    const auto& spec=source.spec;const auto width=spec.pixelSize.width,height=spec.pixelSize.height;
    if(spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true} || !spec.colorSpec.profileId.empty())
        return error(ErrorCode::UnsupportedFeature,"solid-bake.rgb8-unprofiled-only");
    if(!width||!height||width>8192||height>8192||width*height>20000000)
        return error(ErrorCode::BudgetExceeded,"solid-bake.canvas");
    if(newLayer.empty()||source.nodes->contains(newLayer))return error(ErrorCode::InvalidInput,"solid-bake.layer-id");
    const auto unit=[](double v){return std::isfinite(v)&&v>=0&&v<=1;};
    if(!color.profileId.empty()||color.activeChannels!=3||!unit(color.alpha)||!unit(opacity))
        return error(ErrorCode::InvalidInput,"solid-bake.color");
    for(auto c:color.channels)if(!unit(c))return error(ErrorCode::InvalidInput,"solid-bake.color");
    if(color.alpha==0||opacity==0)return error(ErrorCode::InvalidInput,"solid-bake.transparent");
    QtSolidMaskCache cache;
    const auto prepared=cache.prepare(std::span(&shape,1),int(width),int(height),QString::fromLatin1(qVersion()),stop);
    if(prepared!=QtSolidMaskCache::Status::Ok) {
        if(prepared==QtSolidMaskCache::Status::Cancelled)return cancelled();
        if(prepared==QtSolidMaskCache::Status::UnsupportedBackend)return error(ErrorCode::UnsupportedFeature,"solid-bake.backend");
        if(prepared==QtSolidMaskCache::Status::BudgetExceeded || prepared==QtSolidMaskCache::Status::AllocationFailed)
            return error(ErrorCode::BudgetExceeded,"solid-bake.mask-budget");
        return error(ErrorCode::InvalidInput,"solid-bake.mask");
    }
    std::uint64_t x0=width,y0=height,x1=0,y1=0;bool found=false;
    std::vector<std::uint8_t> alpha(256*256);
    for(std::uint64_t y=0;y<height;y+=256)for(std::uint64_t x=0;x<width;x+=256) {
        if(stop.stop_requested())return cancelled();
        const auto w=std::min<std::uint64_t>(256,width-x),h=std::min<std::uint64_t>(256,height-y);
        if(cache.readTile(cache.generation(),shape.id,QRect(int(x),int(y),int(w),int(h)),alpha,w)!=QtSolidMaskCache::Status::Ok)
            return error(ErrorCode::CorruptData,"solid-bake.mask-read");
        for(std::uint64_t row=0;row<h;++row)for(std::uint64_t col=0;col<w;++col)if(alpha[row*w+col]) {
            found=true;x0=std::min(x0,x+col);y0=std::min(y0,y+row);x1=std::max(x1,x+col);y1=std::max(y1,y+row);
        }
    }
    if(!found)return error(ErrorCode::InvalidInput,"solid-bake.empty");
    const Rect roi{std::int64_t(x0),std::int64_t(y0),x1-x0+1,y1-y0+1};
    const croppedRaster::Limits limits;
    if(roi.width*roi.height>(limits.rawBytes-croppedRaster::HeaderBytes)/4)
        return error(ErrorCode::BudgetExceeded,"solid-bake.native-roi-too-large");
    TileData pixels;pixels.width=std::uint32_t(roi.width);pixels.height=std::uint32_t(roi.height);
    pixels.rowStride=roi.width*4;pixels.bytes.resize(pixels.rowStride*roi.height);pixels.format=spec.colorSpec.format;
    QtSolidDraftPaint paint{shape.id,color,opacity};bool visible=false;
    for(std::uint64_t y=0;y<roi.height;y+=512)for(std::uint64_t x=0;x<roi.width;x+=512) {
        if(stop.stop_requested())return cancelled();
        const auto w=std::min<std::uint64_t>(512,roi.width-x),h=std::min<std::uint64_t>(512,roi.height-y);
        TileData transparent;transparent.width=std::uint32_t(w);transparent.height=std::uint32_t(h);
        transparent.rowStride=w*4;transparent.bytes.resize(w*h*4);transparent.format=spec.colorSpec.format;
        auto tile=renderQtSolidDraftPreview(cache,cache.generation(),{roi.x+std::int64_t(x),roi.y+std::int64_t(y),w,h},std::span(&paint,1),transparent,stop);
        if(!tile)return tile.error();
        for(std::size_t at=3;at<tile.value().bytes.size();at+=4)visible|=tile.value().bytes[at]!=0;
        for(std::uint64_t row=0;row<h;++row)std::copy_n(tile.value().bytes.begin()+row*w*4,w*4,pixels.bytes.begin()+(y+row)*pixels.rowStride+x*4);
    }
    if(!visible)return error(ErrorCode::InvalidInput,"solid-bake.quantized-transparent");
    RasterEncodingOptions encoding;encoding.ppi=spec.ppi;
    auto png=encodePngBounded(pixels,encoding,limits.pngBytes,stop);if(!png)return png.error();
    if(stop.stop_requested())return cancelled();
    InputInfo info;info.format=ImageFormat::Png;DecodeOptions decoding;decoding.limits.decodedBytes=limits.rawBytes;
    auto decoded=decodeRasterBytes(png.value(),info,decoding);if(!decoded)return decoded.error();
    if(decoded.value().width!=pixels.width||decoded.value().height!=pixels.height||decoded.value().format!=pixels.format||
       !info.icc.empty()||!info.iccProfile.empty()||!decoded.value().profileId.empty()||decoded.value().rowStride!=pixels.rowStride||decoded.value().bytes!=pixels.bytes)
        return error(ErrorCode::CorruptData,"solid-bake.png-roundtrip");
    if(stop.stop_requested())return cancelled();
    const auto hash=sha256(pixels.bytes);QJsonArray controls;
    for(const auto& p:shape.controls)controls.append(QJsonArray{p.x(),p.y()});
    const QJsonObject manifest{{"producer","Drawborad Qt CPU solid bake"},{"version",1},
        {"qtRuntime",QString::fromLatin1(qVersion())},{"representation","baked-not-editable"},
        {"fillRule","OddEven"},{"resampled",false},{"controls",controls},
        {"nativeCanvas",QJsonArray{double(width),double(height)}},{"ppi",spec.ppi},
        {"roi",QJsonArray{double(roi.x),double(roi.y),double(roi.width),double(roi.height)}},
        {"color",QJsonArray{color.channels[0],color.channels[1],color.channels[2],color.alpha}},
        {"opacity",opacity},{"colorSemantics","existing encoded RGB channels; no ICC; not linear-light"},
        {"rgbaSha256",QString::fromStdString(hash.hex())}};
    const auto json=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if(!QJsonDocument::fromJson(json).isObject())return error(ErrorCode::CorruptData,"solid-bake.manifest");
    Bytes metadata(json.begin(),json.end());
    auto command=croppedRaster::makeNormal(std::move(pixels.bytes),std::move(png.value()),std::move(metadata),roi,width,height,newLayer);
    if(!command)return command.error();
    for(auto& op:command.value().operations)if(auto* node=std::get_if<AddNode>(&op))node->node.name="Qt solid (baked pixels)";
    auto candidate=Document::fromSnapshot(source);if(!candidate)return candidate.error();
    auto applied=candidate.value().apply(command.value(),expectedRevision);if(!applied)return applied.error();
    if(stop.stop_requested())return cancelled();
    return QtSolidRasterCandidate{expectedRevision,std::move(command.value()),roi,hash};
} catch(const std::bad_alloc&) {return error(ErrorCode::BudgetExceeded,"solid-bake.allocation");}
}
