#include "ProjectSession.h"
#include "../../apps/studio/ApplicationController.h"
#include "ui/PaintedDocument.h"
#include "codecs/RasterWriters.h"
#include "color/ColorService.h"
#include "brush/BucketFill.h"
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
        const auto s=snapshot(); QJsonArray nodes,events;
        for(const auto& [id,node]:*s.nodes) {
            nodes.append(QJsonObject{{"id",idText(id)},{"parent_id",idText(node.parent)},
                {"object_id",idText(node.objectId)},{"name",QString::fromStdString(node.name)},
                {"kind",int(node.kind)},{"stage",QString::fromStdString(node.stage)},
                {"visible",node.visible},{"locked",node.locked},{"opacity",node.opacity}});
        }
        for(const auto& e:*s.eventIndex) {
            QJsonArray channels;for(auto x:e.color.channels) channels.append(x);
            events.append(QJsonObject{{"id",idText(e.id)},{"object_id",idText(e.objectId)},
                {"layer_id",idText(e.layerId)},{"tool",QString::fromStdString(e.toolId)},
                {"color",channels},{"alpha",e.color.alpha},{"seed",number(e.seed)},
                {"start_us",number(e.startUs)},{"end_us",number(e.endUs)},
                {"geometry_count",number(e.geometry.size())}});
        }
        return {{"document_id",idText(s.documentId)},{"revision",number(s.revision)},
            {"saved_revision",saved?QJsonValue(number(*saved)):QJsonValue(QJsonValue::Null)},
            {"dirty",controller->dirty()},{"nodes",nodes},{"strokes",events},
            {"stroke_count",number(s.eventIndex->size())},
            {"width",number(s.spec.pixelSize.width)},{"height",number(s.spec.pixelSize.height)},
            {"capabilities",QJsonArray{"recolor_stroke","set_properties","bucket_fill","add_fill_layer","undo","redo","save","save_copy","replay","png","ora"}}};
    }
    QJsonObject request(const QString& method,const QJsonObject& p) {
        if(method=="open" || method=="create") {
            canReplace(p);
            auto next=std::make_unique<ApplicationController>();
            if(method=="open") {
                auto target=pathValue(p.value("path")); checked(next->open(target));path=target;
                saved=next->snapshot()->revision;
            } else {
                auto w=integer(p.value("width")),h=integer(p.value("height"));
                need(w>0 && h>0 && w<=4096 && h<=4096 && w*h<=4*1024*1024,"session.size");
                NewDocumentRequest r;r.dimensions={double(w),double(h),Unit::Px,300};
                checked(next->newDocument(r));path.clear();saved.reset();
            }
            controller=std::move(next);return state();
        }
        if(method=="state") return state();
        if(method=="close") {
            canReplace(p);controller=std::make_unique<ApplicationController>();saved.reset();path.clear();
            return {{"closed",true}};
        }
        if(method=="command") {
            revision(p);auto type=p.value("type").toString();Command command;
            if(type=="recolor_stroke") {
                const auto s=snapshot();auto id=parseId(p.value("stroke_id"));
                auto found=std::find_if(s.eventIndex->begin(),s.eventIndex->end(),[&](const auto& e){return e.id==id;});
                need(found!=s.eventIndex->end(),"stroke.missing");
                auto color=browserColor(s,p.value("color"));color.alpha=found->color.alpha;
                command.operations.emplace_back(RecolorStroke{id,color});
            } else if(type=="add_fill_layer") {
                const auto s=snapshot();LayerNode layer;layer.id=newEventId();
                layer.parent=Document::rootId();layer.name="Color fill";
                layer.bounds={0,0,s.spec.pixelSize.width,s.spec.pixelSize.height};
                command.operations.emplace_back(AddNode{layer});
            } else if(type=="bucket_fill") {
                const auto s=snapshot();
                const auto x=pixelInteger(p.value("x")),y=pixelInteger(p.value("y"));
                const auto tolerance=pixelInteger(p.value("tolerance"));
                need(tolerance<=255,"session.tolerance");
                const auto layer=parseId(p.value("layer_id"));
                const auto color=browserColor(s,p.value("color"));
                auto frame=checked(renderPaintedDocument(s,{s.revision,s.eventIndex->size()}));
                command=checked(bucketFillCommand(s,layer,newEventId(),frame.merged,
                                                  x,y,std::uint8_t(tolerance),color));
            } else if(type=="set_properties") {
                SetProperties properties;properties.id=parseId(p.value("node_id"));bool any=false;
                for(const auto& key:{"visible","locked"}) if(p.contains(key)) {
                    need(p.value(key).isBool(),"session.boolean");any=true;
                    if(QString(key)=="visible") properties.visible=p.value(key).toBool();
                    else properties.locked=p.value(key).toBool();
                }
                need(any,"session.empty-properties");command.operations.emplace_back(properties);
            } else need(false,"session.command");
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
            auto frame=checked(renderPaintedDocument(s,{s.revision,limit}));
            auto format=p.value("format").toString();need(format=="png" || format=="ora","session.format");
            ExportRequest r;r.path=pathValue(p.value("path"));r.size=s.spec.pixelSize;
            r.format=format=="ora"?ExportFormat::Ora:ExportFormat::Png;
            RasterEncodingOptions options;options.ppi=s.spec.ppi;
            if(!s.spec.colorSpec.profileId.empty()) options.iccProfile=*s.resources->at(s.spec.colorSpec.profileId).bytes;
            auto plan=checked(preflightExport(s,r));checked(ExportService{}.write(plan,frame.merged,frame.layers,options));
            return {{"revision",number(s.revision)},{"event_limit",number(limit)},
                {"source_asset_reads",number(frame.statistics.sourceAssetReads)}};
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
