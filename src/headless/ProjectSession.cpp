#include "brush/HiddenBoundaryFill.h"
#include "core/CroppedStrokeMask.h"
#include "ProjectSession.h"
#include "view/LayerTree.h"
#include "../../apps/studio/ApplicationController.h"
#include "ui/PaintedDocument.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include "codecs/OraStream.h"
#include "codecs/RasterWriters.h"
#include "color/ColorService.h"
#include "brush/BucketFill.h"
#include "brush/FreehandStroke.h"
#include "brush/PressureTaper.h"
#include "brush/DryInkStroke.h"
#include "core/LineartRaster.h"
#include "core/MyPaintCroppedRaster.h"
#include "core/Digest.h"
#include "codecs/ImageReader.h"
#include "storage/FileIO.h"
#include <cmath>
#include <random>
#include <QColorSpace>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <iostream>
#include <memory>
namespace drawborad {
namespace {
struct Failure { Error value; };
template<class T> T checked(Result<T> result) {
    if (!result) throw Failure{result.error()};
    return std::move(result.value());
}
void checked(Result<void> result) { if (!result) throw Failure{result.error()}; }
void need(bool value,const char* key) {
    if (!value) throw Failure{error(ErrorCode::InvalidInput,key)};
}
QString number(std::uint64_t v) { return QString::number(v); }
std::uint32_t pixelInteger(QJsonValue value) {
    const auto n=value.toDouble(-1);
    need(value.isDouble() && std::isfinite(n) && n>=0 && n<=UINT32_MAX &&
         std::floor(n)==n,"session.pixel-integer");
    return std::uint32_t(n);
}
Id newEventId() {
    std::random_device random;Id id;
    for (auto& b:id.bytes) b=std::uint8_t(random());
    id.bytes[6]=(id.bytes[6]&15)|0x40;id.bytes[8]=(id.bytes[8]&63)|0x80;
    return id;
}
QString idText(Id id) {
    return QByteArray(reinterpret_cast<const char*>(id.bytes.data()),16).toHex();
}
Id parseId(QJsonValue value) {
    auto text=value.toString();
    need(QRegularExpression("^[0-9a-f]{32}$").match(text).hasMatch(),"session.id");
    auto bytes=QByteArray::fromHex(text.toLatin1()); Id id;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()),16,id.bytes.begin());
    return id;
}
std::uint64_t integer(QJsonValue value) {
    auto text=value.toString(); bool ok=false;
    auto n=text.toULongLong(&ok);
    need(ok && number(n)==text,"session.decimal-string"); return n;
}
std::filesystem::path pathValue(QJsonValue v) {
    auto s=v.toString(); need(!s.isEmpty() && !s.contains(QChar(0)),"session.path");
    return std::filesystem::path(s.toUtf8().toStdString());
}
QString errorName(ErrorCode code) {
    switch(code) {
    case ErrorCode::StaleRevision:return "stale_revision";
    case ErrorCode::Locked:return "locked";
    case ErrorCode::IoError:return "io_error";
    case ErrorCode::UnsupportedFeature:return "unsupported";
    default:return "invalid_input";
    }
}
ColorValue browserColor(const DocumentSnapshot& snapshot,QJsonValue value) {
    auto text=value.toString();
    need(QRegularExpression("^#[0-9a-fA-F]{6}$").match(text).hasMatch(),"session.color");
    need(snapshot.spec.colorSpec.format.mode==ColorMode::RGB,"session.rgb-only");
    auto raw=QByteArray::fromHex(text.mid(1).toLatin1());
    Bytes channels; for(auto c:raw) channels.push_back(std::uint8_t(c));
    auto profile=snapshot.spec.colorSpec.profileId;
    if (!profile.empty()) {
        ProfileStore profiles;
        const auto icc=QColorSpace(QColorSpace::SRgb).iccProfile();
        auto source=checked(profiles.import(std::span(reinterpret_cast<const std::uint8_t*>(icc.constData()),std::size_t(icc.size()))));
        need(snapshot.resources->contains(profile),"session.profile-missing");
        checked(profiles.import(*snapshot.resources->at(profile).bytes));
        TileData pixel; pixel.format={ColorMode::RGB,Depth::U8,false}; pixel.width=1;pixel.height=1;
        pixel.rowStride=3;pixel.bytes=channels;pixel.profileId=source;
        channels=checked(ColorService(profiles).convert(pixel,{source,profile})).bytes;
    }
    ColorValue result; result.profileId=profile;
    for(std::size_t i=0;i<3;++i) result.channels[i]=double(channels[i])/255.;
    return result;
}
class Session {
    std::unique_ptr<ApplicationController> controller=std::make_unique<ApplicationController>();
    std::optional<Revision> saved;
    std::filesystem::path path;
    DocumentSnapshot snapshot() {
        auto s=controller->snapshot(); need(s.has_value(),"session.no-project"); return *s;
    }
    void revision(const QJsonObject& params) {
        if(integer(params.value("expected_revision"))!=snapshot().revision)
            throw Failure{error(ErrorCode::StaleRevision,"session.revision")};
    }
    void canReplace(const QJsonObject& params) {
        if(controller->snapshot()) {
            revision(params);
            need(!controller->dirty() || params.value("discard")==QJsonValue(true),"session.unsaved-changes");
        }
    }
public:
    QJsonObject state() {
        const auto s=snapshot(); QJsonArray nodes,events,boundaries;
        for(const auto& [id,node]:*s.nodes) {
            if(node.stage==hiddenBoundary::Stage){const auto b=checked(hiddenBoundary::parse(s,node));QJsonArray points;for(const auto&point:b.points)points.append(QJsonObject{{"x",point.x},{"y",point.y}});boundaries.append(QJsonObject{{"id",idText(id)},{"lineart_id",idText(b.lineart)},{"enabled",b.enabled},{"radius",b.radius},{"points",points}});}
            QJsonArray children;for(const auto child:node.children)children.append(idText(child));
            QJsonValue strokeMask(QJsonValue::Null);
            if(!node.maskId.empty() && cropMask::tagged(s.resources->at(node.maskId))){
                const auto v=checked(cropMask::parse(s,s.resources->at(node.maskId)));
                strokeMask=QJsonObject{{"enabled",v.enabled},{"rect",QJsonObject{{"x",double(v.rect.x)},{"y",double(v.rect.y)},{"width",double(v.rect.width)},{"height",double(v.rect.height)}}}};
            }
            nodes.append(QJsonObject{{"id",idText(id)},{"parent_id",idText(node.parent)},
                {"object_id",idText(node.objectId)},{"name",QString::fromStdString(node.name)},
                {"kind",int(node.kind)},{"stage",QString::fromStdString(node.stage)},
                {"source_kind",int(node.sourceKind)},{"blend",int(node.blend)},{"children",children},
                {"visible",node.visible},{"locked",node.locked},{"opacity",node.opacity},{"mask_supported",cropMask::supported(s,id)},{"stroke_mask",strokeMask}});
        }
        for(const auto& e:*s.eventIndex) {
            QJsonArray channels;for(auto x:e.color.channels) channels.append(x);
            events.append(QJsonObject{{"id",idText(e.id)},{"object_id",idText(e.objectId)},
                {"layer_id",idText(e.layerId)},{"tool",QString::fromStdString(e.toolId)},
                {"color",channels},{"alpha",e.color.alpha},{"seed",number(e.seed)},
                {"start_us",number(e.startUs)},{"end_us",number(e.endUs)},
                {"geometry_count",number(e.geometry.size())}});
        }
        QJsonArray capabilities{"reorder_layer","mouse_pressure_taper","duplicate_paint_layer","recolor_layer","recolor_stroke","set_properties","bucket_fill","add_fill_layer","draw_stroke","update_layer","undo","redo","save","save_copy","replay","png"};
        capabilities.append("smooth_ink");capabilities.append("ora");capabilities.append("append_brush_layer");capabilities.append("append_brush_crop");capabilities.append("append_brush_normal");capabilities.append("cropped_stroke_mask");capabilities.append("hidden_fill_boundary");
        return {{"document_id",idText(s.documentId)},{"revision",number(s.revision)},
            {"saved_revision",saved?QJsonValue(number(*saved)):QJsonValue(QJsonValue::Null)},
            {"dirty",controller->dirty()},{"hidden_boundaries",boundaries},{"nodes",nodes},{"strokes",events},
            {"stroke_count",number(s.eventIndex->size())},
            {"width",number(s.spec.pixelSize.width)},{"height",number(s.spec.pixelSize.height)},{"ppi",s.spec.ppi},
            {"capabilities",capabilities}};
    }
    QJsonObject request(const QString& method,const QJsonObject& p) {
        if(method=="import_lineart" || method=="append_brush_layer" || method=="append_brush_crop") {
            const bool crop=method=="append_brush_crop";
            const bool append=method=="append_brush_layer" || crop;
            const auto paintMode=p.value("paint_mode");
            need(paintMode.isUndefined() || (paintMode.isString() && (paintMode.toString()=="multiply" || paintMode.toString()=="normal")),"brush-crop.paint-mode");
            const bool normal=paintMode.toString()=="normal";
            need(!normal || crop,"brush-crop.normal-requires-crop");
            if(append) {
                revision(p);
                if(parseId(p.value("expected_document_id"))!=snapshot().documentId)
                    throw Failure{error(ErrorCode::StaleRevision,"session.document-changed")};
            } else canReplace(p);
            const auto source=pathValue(p.value("path"));
            ResourceBudget budget;budget.scratchBytes=128*1024*1024;budget.ramReclaimBytes=128*1024*1024;
            const std::uint64_t sourceLimit=(crop?16:128)*1024*1024;
            const auto info=checked(preflightInput(source,budget,sourceLimit));
            need(info.format==ImageFormat::Png,"lineart.png-required");
            DecodeOptions options;options.limits.encodedBytes=sourceLimit;options.limits.decodedBytes=sourceLimit;
            auto imported=checked(decodeImage(source,options));
            need(imported.decodedPixels.has_value(),"lineart.decode");
            auto pixels=std::move(*imported.decodedPixels);
            need(pixels.format.mode==ColorMode::RGB && pixels.format.depth==Depth::U8 &&
                 pixels.rowStride==std::uint64_t(pixels.width)*pixels.format.channelCount(),"lineart.rgb8-required");
            need(pixels.width<=8192 && pixels.height<=8192 && std::uint64_t(pixels.width)*pixels.height<=20000000,"lineart.size");
            if(crop)need(pixels.width<=2048 && pixels.height<=2048 && std::uint64_t(pixels.width)*pixels.height<=1048576,"brush-crop.pixel-budget");
            if(!pixels.format.alpha) {
                Bytes rgba(std::size_t(pixels.width)*pixels.height*4);
                for(std::size_t i=0;i<rgba.size()/4;++i) {
                    std::copy_n(pixels.bytes.begin()+std::ptrdiff_t(i*3),3,rgba.begin()+std::ptrdiff_t(i*4));rgba[i*4+3]=255;
                }
                pixels.bytes=std::move(rgba);pixels.format.alpha=true;pixels.rowStride=std::uint64_t(pixels.width)*4;
            }
            if(!imported.info.iccProfile.empty()) {
                const auto& bytes=imported.info.iccProfile;
                auto space=QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())));
                need(space.isValid() && space==QColorSpace(QColorSpace::SRgb),"lineart.non-srgb-profile-not-supported");
            }
            if(append) {
                // Worker receipts describe geometry, not actual ink coverage.
                // Reject invisible strokes before installing resources or changing revision.
                bool hasInk=false;
                for(std::size_t i=3;i<pixels.bytes.size();i+=4)if(pixels.bytes[i]){hasInk=true;break;}
                need(hasInk,"brush-layer.no-visible-ink");
                const auto current=snapshot();
                if(!crop)need(pixels.width==current.spec.pixelSize.width && pixels.height==current.spec.pixelSize.height,"brush-layer.canvas-mismatch");
                auto manifestBytes=checked(NativeFileIO{}.read(pathValue(p.value("manifest_path")),1024*1024));
                QJsonParseError parse;
                const auto manifest=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(manifestBytes.data()),qsizetype(manifestBytes.size())),&parse);
                need(parse.error==QJsonParseError::NoError && manifest.isObject(),"brush-layer.manifest-json");
                if(crop) {
                    const auto x=integer(p.value("origin_x")),y=integer(p.value("origin_y"));
                    need(x<=8192 && y<=8192,"brush-crop.origin");
                    Rect roi{std::int64_t(x),std::int64_t(y),pixels.width,pixels.height};
                    need(croppedRaster::fits(roi,current.spec.pixelSize.width,current.spec.pixelSize.height),"brush-crop.bounds");
                    const auto meta=manifest.object();
                    need(meta.value("prototype").toString()=="mypaint-cropped-study-v1","brush-crop.manifest-format");
                    const auto origin=meta.value("origin").toArray(),size=meta.value("roi_size").toArray(),canvas=meta.value("full_canvas").toArray();
                    need(origin.size()==2 && size.size()==2 && canvas.size()==2,"brush-crop.manifest-layout");
                    auto metaInteger=[](QJsonValue v)->std::uint64_t {
                        const double d=v.toDouble(-1);need(v.isDouble() && std::isfinite(d) && d>=0 && d<=8192 && std::floor(d)==d,"brush-crop.manifest-number");return std::uint64_t(d);
                    };
                    need(metaInteger(origin.at(0))==x && metaInteger(origin.at(1))==y && metaInteger(size.at(0))==pixels.width && metaInteger(size.at(1))==pixels.height && metaInteger(canvas.at(0))==current.spec.pixelSize.width && metaInteger(canvas.at(1))==current.spec.pixelSize.height,"brush-crop.manifest-placement");
                    need(meta.value("original_global_points").isArray() && !meta.value("original_global_points").toArray().isEmpty() && meta.value("translated_local_points").isArray() && meta.value("worker_manifest").isObject(),"brush-crop.manifest-inputs");
                    need(meta.value("png_sha256").toString().toStdString()==sha256(imported.encodedSource).hex(),"brush-crop.png-digest");
                    const auto globals=meta.value("original_global_points").toArray(),locals=meta.value("translated_local_points").toArray();
                    need(globals.size()==locals.size() && globals.size()<=1024,"brush-crop.point-count");
                    double last=-1;
                    for(qsizetype i=0;i<globals.size();++i) {
                        need(globals.at(i).isObject() && locals.at(i).isObject(),"brush-crop.point-object");
                        const auto g=globals.at(i).toObject(),l=locals.at(i).toObject();
                        auto scalar=[](const QJsonObject& o,const char* key) {const auto v=o.value(key);const double d=v.toDouble();need(v.isDouble() && std::isfinite(d),"brush-crop.point-finite");return d;};
                        const double gx=scalar(g,"x"),gy=scalar(g,"y"),pressure=scalar(g,"pressure"),time=scalar(g,"time_ms");
                        need(gx>=0 && gy>=0 && gx<current.spec.pixelSize.width && gy<current.spec.pixelSize.height && pressure>=0 && pressure<=1 && time>=0 && time<=60000 && time>=last,"brush-crop.point-bounds");
                        need(scalar(l,"x")>=0 && scalar(l,"x")<pixels.width && scalar(l,"y")>=0 && scalar(l,"y")<pixels.height,"brush-crop.local-bounds");
                        need(std::abs(scalar(l,"x")-(gx-double(x)))<1e-6 && std::abs(scalar(l,"y")-(gy-double(y)))<1e-6 && scalar(l,"pressure")==pressure && scalar(l,"time_ms")==time,"brush-crop.point-translation");last=time;
                    }
                    const auto worker=meta.value("worker_manifest").toObject();
                    const auto recordedMode=worker.value("paint_mode");
                    need(recordedMode.isUndefined() || (recordedMode.isString() && (recordedMode.toString()=="multiply" || recordedMode.toString()=="normal")),"brush-crop.manifest-paint-mode");
                    need(normal ? recordedMode.toString()=="normal" : (recordedMode.isUndefined() || recordedMode.toString()=="multiply"),"brush-crop.paint-mode-mismatch");
                    need(worker.value("document_id").toString()==idText(current.documentId),"brush-crop.worker-document");
                    const auto guard=worker.value("actual_dab_guard").toObject();
                    const auto dabs=guard.value("checked_dabs");const double dabCount=dabs.toDouble(-1);
                    need(guard.value("passed")==QJsonValue(true) && dabs.isDouble() && std::isfinite(dabCount) && dabCount>=0 && dabCount<=10000000 && std::floor(dabCount)==dabCount,"brush-crop.guard-proof");
                    need(worker.value("points").toArray()==locals && worker.value("canvas").toArray()==size,"brush-crop.worker-inputs");
                    auto command=checked((normal?croppedRaster::makeNormal:croppedRaster::make)(std::move(pixels.bytes),std::move(imported.encodedSource),std::move(manifestBytes),roi,current.spec.pixelSize.width,current.spec.pixelSize.height,newEventId(),{}));
                    // Preflight before copying/applying the command. Shared resource IDs count once.
                    std::uint64_t total=0;constexpr std::uint64_t limit=256ull*1024*1024;
                    for(const auto& [id,res]:*current.resources) {
                        (void)id;need(bool(res.bytes),"brush-crop.resource-bytes");
                        need(res.bytes->size()<=limit-total,"brush-crop.total-budget");total+=res.bytes->size();
                    }
                    for(const auto& op:command.operations)if(const auto* install=std::get_if<InstallResource>(&op)) {
                        if(current.resources->contains(install->resource.id))continue;
                        const auto bytes=install->resource.bytes->size();
                        need(bytes<=limit-total,"brush-crop.total-budget");total+=bytes;
                    }
                    auto candidate=checked(Document::fromSnapshot(current));checked(candidate.apply(command,current.revision));
                    checked(planMergedDocument(candidate.snapshot()));checked(controller->apply(command));return state();
                }
                Resource replay;replay.kind=ResourceKind::Reference;replay.id=sha256(manifestBytes);
                replay.bytes=std::make_shared<const Bytes>(std::move(manifestBytes));
                auto command=checked(makeLineartRasterCommand(std::move(pixels.bytes),std::move(imported.encodedSource),pixels.width,pixels.height,newEventId()));
                // Preserve raw -> PNG's single dependency contract. PNG -> manifest
                // retains exact replay inputs without pretending they are native events.
                auto& png=std::get<InstallResource>(command.operations.at(0)).resource;
                png.dependencies={replay.id};
                auto& layer=std::get<AddNode>(command.operations.at(2)).node;
                layer.name="MyPaint ink (experimental raster)";
                command.operations.insert(command.operations.begin(),InstallResource{std::move(replay)});
                auto candidate=checked(Document::fromSnapshot(current));
                checked(candidate.apply(command,current.revision));
                checked(planMergedDocument(candidate.snapshot()));
                checked(controller->apply(command));return state();
            }
            auto next=std::make_unique<ApplicationController>();
            NewDocumentRequest r;r.dimensions={double(pixels.width),double(pixels.height),Unit::Px,300};
            r.transparent=false;r.paperColor.channels={1,1,1,0};r.paperColor.alpha=1;
            checked(next->newDocument(r));
            checked(next->apply(checked(makeLineartRasterCommand(std::move(pixels.bytes),std::move(imported.encodedSource),pixels.width,pixels.height,newEventId()))));
            checked(planMergedDocument(*next->snapshot()));
            controller=std::move(next);path.clear();saved.reset();return state();
        }
        if(method=="open" || method=="create") {
            canReplace(p);
            auto next=std::make_unique<ApplicationController>();
            if(method=="open") {
                auto target=pathValue(p.value("path")); checked(next->open(target));path=target;
                saved=next->snapshot()->revision;
            } else {
                auto w=integer(p.value("width")),h=integer(p.value("height"));
                need(w>0 && h>0 && w<=8192 && h<=8192 && w*h<=20000000,"session.size");
                NewDocumentRequest r;r.dimensions={double(w),double(h),Unit::Px,300};
                checked(next->newDocument(r));
                checked(planMergedDocument(*next->snapshot()));path.clear();saved.reset();
            }
            controller=std::move(next);return state();
        }
        if(method=="state") return state();
        if(method=="close") {
            canReplace(p);controller=std::make_unique<ApplicationController>();saved.reset();path.clear();
            return {{"closed",true}};
        }
        if(method=="add_hidden_boundary" || method=="toggle_hidden_boundary" || method=="hidden_boundary_fill") {
            revision(p);const auto s=snapshot();if(parseId(p.value("expected_document_id"))!=s.documentId)throw Failure{error(ErrorCode::StaleRevision,"session.document")};Command command;
            if(method=="add_hidden_boundary"){
                need(p.value("points").isArray()&&p.value("radius").isDouble(),"boundary.input");const auto values=p.value("points").toArray();need(values.size()>=2&&values.size()<=256,"boundary.points");std::vector<hiddenBoundary::Point> points;
                for(const auto& value:values){need(value.isObject(),"boundary.point");const auto point=value.toObject();need(point.size()==2&&point.value("x").isDouble()&&point.value("y").isDouble(),"boundary.point");points.push_back({point.value("x").toDouble(),point.value("y").toDouble()});}
                command=checked(hiddenBoundary::add(s,newEventId(),parseId(p.value("lineart_id")),points,p.value("radius").toDouble()));
            }else if(method=="toggle_hidden_boundary"){
                need(p.value("enabled").isBool(),"boundary.enabled");command=checked(hiddenBoundary::toggle(s,parseId(p.value("boundary_id")),p.value("enabled").toBool()));
            }else{
                const auto threshold=pixelInteger(p.value("source_luminance_threshold"));need(threshold<=255,"boundary.threshold");
                command=checked(hiddenBoundaryFillCommand(s,parseId(p.value("lineart_id")),parseId(p.value("layer_id")),newEventId(),pixelInteger(p.value("x")),pixelInteger(p.value("y")),std::uint8_t(threshold),browserColor(s,p.value("color"))));
            }
            auto candidate=checked(Document::fromSnapshot(s));checked(candidate.apply(command,s.revision));checked(planMergedDocument(candidate.snapshot()));checked(controller->apply(command));return state();
        }
        if(method=="set_stroke_mask" || method=="toggle_stroke_mask") {
            revision(p);const auto s=snapshot();
            if(parseId(p.value("expected_document_id"))!=s.documentId)throw Failure{error(ErrorCode::StaleRevision,"session.document")};
            const auto id=parseId(p.value("layer_id"));Command command;
            if(method=="set_stroke_mask"){
                need(p.value("rect").isObject(),"stroke-mask.rect");const auto rect=p.value("rect").toObject();
                auto coordinate=[&](const char* key){const auto value=rect.value(key);need(value.isDouble()&&std::isfinite(value.toDouble())&&value.toDouble()>=0&&value.toDouble()<=8192&&std::floor(value.toDouble())==value.toDouble(),"stroke-mask.coordinate");return std::uint64_t(value.toDouble());};
                const auto x=coordinate("x"),y=coordinate("y"),w=coordinate("width"),h=coordinate("height");
                need(x<=8192&&y<=8192&&w<=8192&&h<=8192,"stroke-mask.bounds");
                command=checked(cropMask::set(s,id,{std::int64_t(x),std::int64_t(y),w,h},true));
            }else{
                need(p.value("enabled").isBool(),"stroke-mask.enabled");command=checked(cropMask::toggle(s,id,p.value("enabled").toBool()));
            }
            auto candidate=checked(Document::fromSnapshot(s));checked(candidate.apply(command,s.revision));checked(planMergedDocument(candidate.snapshot()));
            checked(controller->apply(command));return state();
        }
        if(method=="command") {
            revision(p);auto type=p.value("type").toString();Command command;
            if(type=="recolor_stroke") {
                const auto s=snapshot();auto id=parseId(p.value("stroke_id"));
                auto found=std::find_if(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto& e){return e.id==id;});
                need(found!=s.eventIndex->end(),"stroke.missing");
                auto color=browserColor(s,p.value("color"));color.alpha=found->color.alpha;
                command.operations.emplace_back(RecolorStroke{id,color});
            } else if(type=="duplicate_paint_layer") {
                const auto s=snapshot();const auto source=parseId(p.value("layer_id"));
                need(nativeLayerPaintable(s,source),"duplicate-paint.unsupported-or-locked");
                const auto& original=s.nodes->at(source);
                need(original.objectId.empty() && original.maskId.empty() && original.children.empty(),"duplicate-paint.plain-layer-required");
                auto node=original;node.id=newEventId();node.name="Copy - "+original.name;node.visible=true;
                node.provenance.push_back(source);
                const auto& siblings=s.nodes->at(original.parent).children;
                const auto index=std::size_t(std::find(siblings.begin(),siblings.end(),source)-siblings.begin());
                command.operations.emplace_back(AddNode{node,index+1});
                std::size_t count=0;
                for(const auto& originalEvent:*s.eventIndex)if(originalEvent.layerId==source){
                    auto event=originalEvent;event.id=newEventId();event.layerId=node.id;event.parentRevision=s.revision;
                    command.operations.emplace_back(Paint{std::move(event),node.bounds});++count;
                }
                need(count>0,"duplicate-paint.empty");
                // Preflight while visible, then hide the backup. This checks the
                // extra rendering work too, not just a cheap hidden-layer state.
                auto candidate=checked(Document::fromSnapshot(s));checked(candidate.apply(command,s.revision));
                checked(planMergedDocument(candidate.snapshot()));
                SetProperties hidden;hidden.id=node.id;hidden.visible=false;command.operations.emplace_back(hidden);
            } else if(type=="recolor_layer") {
                const auto s=snapshot();const auto layer=parseId(p.value("layer_id"));
                need(nativeLayerPaintable(s,layer),"recolor-layer.unsupported-or-locked");
                const auto target=browserColor(s,p.value("color"));
                for(const auto& event:*s.eventIndex)if(event.layerId==layer){
                    auto color=target;color.alpha=event.color.alpha;
                    command.operations.emplace_back(RecolorStroke{event.id,color});
                }
                need(!command.operations.empty(),"recolor-layer.empty");
            } else if(type=="add_fill_layer") {
                command=checked(addFillLayerCommand(snapshot(),newEventId(),true,true));
            } else if(type=="bucket_fill") {
                const auto s=snapshot();
                const auto x=pixelInteger(p.value("x")),y=pixelInteger(p.value("y"));
                const auto tolerance=pixelInteger(p.value("tolerance"));
                need(tolerance<=255,"session.tolerance");
                const auto underInk=p.contains("under_ink")?pixelInteger(p.value("under_ink")):0;
                need(underInk<=2,"session.under-ink");
                const auto layer=parseId(p.value("layer_id"));
                const auto color=browserColor(s,p.value("color"));
                auto frame=checked(renderMergedDocument(s,{s.revision,s.eventIndex->size()}));
                command=checked(bucketFillCommand(s,layer,newEventId(),frame.merged,
                                                  x,y,std::uint8_t(tolerance),color,{},std::uint8_t(underInk)));
            } else if(type=="draw_stroke") {
                const auto s=snapshot();const auto width=p.value("width");
                need(width.isDouble() && std::isfinite(width.toDouble()),"session.pen-width");
                need(p.value("points").isArray(),"session.pen-points");
                const auto points=p.value("points").toArray();
                need(!points.isEmpty() && points.size()<=1024,"session.pen-points");
                std::vector<InputSample> samples;samples.reserve(points.size());
                for(const auto& value:points) {
                    need(value.isObject(),"session.pen-point");const auto point=value.toObject();
                    need((point.size()==2 || (point.size()==3 && point.contains("pressure"))) && point.value("x").isDouble() && point.value("y").isDouble(),"session.pen-point");
                    InputSample sample;sample.x=point.value("x").toDouble();sample.y=point.value("y").toDouble();
                    need(std::isfinite(sample.x) && std::isfinite(sample.y),"session.pen-point");
                    sample.pressure=1;
                    if(point.contains("pressure")) {
                        need(point.value("pressure").isDouble(),"session.pen-pressure");
                        sample.pressure=point.value("pressure").toDouble();
                        need(std::isfinite(sample.pressure) && sample.pressure>=0 && sample.pressure<=1,"session.pen-pressure");
                    }
                    sample.timestampUs=samples.size();samples.push_back(sample);
                }
                if(p.contains("pressure_taper")) {
                    need(p.value("pressure_taper").isDouble(),"session.pressure-taper");
                    samples=checked(taperMousePressure(samples,p.value("pressure_taper").toDouble()));
                }
                const auto brush=p.value("brush").toString("fine");
                need(!p.contains("brush") || p.value("brush").isString(),"session.brush");
                need(brush=="fine" || brush=="dry_ink" || brush=="smooth_ink","session.brush");
                const auto layer=parseId(p.value("layer_id"));const auto event=newEventId();
                const auto color=browserColor(s,p.value("color"));
                command=checked(brush=="dry_ink" ? dryInkStrokeCommand(s,layer,event,samples,width.toDouble(),color)
                    : brush=="smooth_ink" ? smoothInkStrokeCommand(s,layer,event,samples,width.toDouble(),color)
                    : freehandStrokeCommand(s,layer,event,samples,width.toDouble(),color));
            } else if(type=="reorder_layer") {
                const auto s=snapshot();const auto id=parseId(p.value("layer_id"));
                const auto it=s.nodes->find(id);
                need(it!=s.nodes->end() && it->second.kind==NodeKind::Layer && it->second.children.empty(),"reorder.plain-layer-required");
                const auto direction=p.value("direction").toString();
                need(direction=="up" || direction=="down","reorder.direction");
                const auto parent=it->second.parent;const auto& children=s.nodes->at(parent).children;
                const auto pos=std::find(children.begin(),children.end(),id);
                need(pos!=children.end(),"reorder.parent");const auto index=std::size_t(pos-children.begin());
                need(direction=="up"?index+1<children.size():index>0,"reorder.edge");
                command.operations.emplace_back(MoveNode{id,parent,direction=="up"?index+1:index-1});
                auto candidate=checked(Document::fromSnapshot(s));checked(candidate.apply(command,s.revision));
                checked(planMergedDocument(candidate.snapshot()));
            } else if(type=="update_layer") {
                const auto id=parseId(p.value("node_id"));bool any=false;
                need(id!=Document::rootId(),"session.root-layer");
                if(p.contains("name")) {
                    need(p.value("name").isString(),"session.layer-name");
                    const auto name=p.value("name").toString().trimmed().toUtf8();
                    need(!name.isEmpty() && name.size()<=256 && !name.contains(char(0)),"session.layer-name");
                    command.operations.emplace_back(RenameNode{id,name.toStdString()});any=true;
                }
                if(p.contains("opacity")) {
                    const auto value=p.value("opacity");
                    need(value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble()>=0 && value.toDouble()<=1,"session.layer-opacity");
                    SetProperties properties;properties.id=id;properties.opacity=value.toDouble();
                    command.operations.emplace_back(properties);any=true;
                }
                need(any,"session.empty-properties");
            } else if(type=="set_properties") {
                SetProperties properties;properties.id=parseId(p.value("node_id"));bool any=false;
                for(const auto& key:{"visible","locked"}) if(p.contains(key)) {
                    need(p.value(key).isBool(),"session.boolean");any=true;
                    if(QString(key)=="visible") properties.visible=p.value(key).toBool();
                    else properties.locked=p.value(key).toBool();
                }
                need(any,"session.empty-properties");command.operations.emplace_back(properties);
            } else need(false,"session.command");
            if(type=="bucket_fill" || type=="add_fill_layer" || type=="draw_stroke") {
                const auto s=snapshot();auto candidate=checked(Document::fromSnapshot(s));
                checked(candidate.apply(command,s.revision));
                checked(planMergedDocument(candidate.snapshot()));
            }
            checked(controller->apply(command));return state();
        }
        if(method=="undo" || method=="redo") {
            revision(p);checked(method=="undo"?controller->undo():controller->redo());return state();
        }
        if(method=="save" || method=="save_copy") {
            revision(p);auto target=p.contains("path")?pathValue(p.value("path")):path;
            if(method=="save_copy") {
                need(target!=path && !std::filesystem::exists(target),
                     "session.copy-needs-new-path");
                for(const auto suffix:{".bak",".journal"}) {
                    auto sidecar=target;sidecar+=suffix;
                    need(!std::filesystem::exists(sidecar),"session.copy-sidecar");
                }
            }
            need(!target.empty(),"session.path");checked(controller->save(target));path=target;
            saved=snapshot().revision;return state();
        }
        if(method=="export") {
            auto s=snapshot();auto limit=p.contains("event_limit")?integer(p.value("event_limit")):s.eventIndex->size();
            need(limit<=s.eventIndex->size(),"session.event-limit");
            auto format=p.value("format").toString();need(format=="png" || format=="ora","session.format");
            ExportRequest r;r.path=pathValue(p.value("path"));r.size=s.spec.pixelSize;
            r.format=format=="ora"?ExportFormat::Ora:ExportFormat::Png;
            RasterEncodingOptions options;options.ppi=s.spec.ppi;
            if(!s.spec.colorSpec.profileId.empty()) options.iccProfile=*s.resources->at(s.spec.colorSpec.profileId).bytes;
            auto plan=checked(preflightExport(s,r));RenderStatistics statistics;QJsonArray warnings;
            if(format=="png") {
                auto frame=checked(renderMergedDocument(s,{s.revision,limit}));
                checked(ExportService{}.write(plan,frame.merged,{},options));statistics=frame.statistics;
            } else {
                auto stream=checked(NativeLayerStream::create(s,{s.revision,limit}));
                auto frame=checked(renderMergedDocument(s,{s.revision,limit}));
                auto report=checked(writeOraStream(plan,stream.size(),
                    [&stream](std::size_t index,std::stop_token stop){return stream.read(index,stop);},
                    frame.merged,options));
                for(const auto& warning:report.warnings)warnings.append(QString::fromStdString(warning));
                if(std::any_of(s.nodes->begin(),s.nodes->end(),[](const auto& item){return item.second.kind==NodeKind::Group || item.second.kind==NodeKind::Object;}))
                    warnings.append("ora-groups-flattened-to-leaves");
                if(std::any_of(s.nodes->begin(),s.nodes->end(),[](const auto& item){return !item.second.maskId.empty();}))
                    warnings.append("ora-masks-baked-into-layer-pixels");
                statistics=stream.statistics();
            }
            return {{"revision",number(s.revision)},{"event_limit",number(limit)},
                {"source_asset_reads",number(statistics.sourceAssetReads)},{"warnings",warnings}};
        }
        need(false,"session.method");return {};
    }
};
}
int runProjectSession() {
    Session session;std::string line;
    while(std::cin.good()) {
        line.clear();bool oversized=false;char ch;
        while(std::cin.get(ch) && ch!='\n') {
            if(line.size()<65536) line.push_back(ch);
            else oversized=true;
        }
        if(line.empty() && !oversized && std::cin.eof()) break;
        QJsonObject response;
        try {
            need(!oversized,"session.request-size");QJsonParseError parse;
            auto doc=QJsonDocument::fromJson(QByteArray::fromStdString(line),&parse);
            need(parse.error==QJsonParseError::NoError && doc.isObject(),"session.json");auto req=doc.object();
            response.insert("request_id",req.value("request_id"));
            need(req.value("request_id").isString() && req.value("params").isObject(),"session.envelope");
            response.insert("result",session.request(req.value("method").toString(),req.value("params").toObject()));
        } catch(const Failure& f) {
            response.insert("error",QJsonObject{{"code",errorName(f.value.code)},
                {"message",QString::fromStdString(f.value.messageKey)}});
        } catch(const std::exception&) {
            response.insert("error",QJsonObject{{"code","engine_error"},{"message","session.exception"}});
        }
        std::cout<<QJsonDocument(response).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
    return 0;
}
}
