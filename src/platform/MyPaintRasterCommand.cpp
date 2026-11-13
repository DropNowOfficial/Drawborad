#include "MyPaintRasterCommand.h"
#include "core/MyPaintCroppedRaster.h"
#include "codecs/RasterReaders.h"
#include "ui/MergedDocument.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
namespace drawborad {
namespace {
constexpr auto Marker="drawborad.mypaint.desktop.stroke.v1";
constexpr auto FolderStage="drawborad.mypaint.folder.v1";
struct Failure { Error error; };
void need(bool yes,const char* key,ErrorCode code=ErrorCode::InvalidInput){if(!yes)throw Failure{error(code,key)};}
QByteArray read(const QString& path,qint64 cap){QFileInfo info(path);need(info.isFile()&&!info.isSymLink()&&info.size()>0&&info.size()<=cap,"mypaint.file-size");QFile f(path);need(f.open(QIODevice::ReadOnly),"mypaint.file-open");auto bytes=f.read(cap+1);need(bytes.size()<=cap&&bytes.size()==info.size(),"mypaint.file-change");return bytes;}
QString hash(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QJsonObject json(const QByteArray& bytes){QJsonParseError err;const auto doc=QJsonDocument::fromJson(bytes,&err);need(err.error==QJsonParseError::NoError&&doc.isObject(),"mypaint.json");return doc.object();}
QJsonObject pins(const MyPaintRuntime& r){auto p=json(read(r.pins,16384));need(p.size()==4,"mypaint.pins-schema");for(const auto* k:{"libmypaint","json_c","preset","abi"}){const auto s=p[k].toString();need(s.size()==64&&std::all_of(s.begin(),s.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"mypaint.pins-schema");}return p;}
QString idText(Id id){return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.bytes.data()),16).toHex());}
bool unit(double n){return std::isfinite(n)&&n>=0&&n<=1;}
bool eligible(const DocumentSnapshot& s,NodeId id){auto it=s.nodes->find(id);if(it==s.nodes->end())return false;const auto& n=it->second;const auto& root=s.nodes->at(Document::rootId());return n.kind==NodeKind::Group&&n.stage==FolderStage&&n.parent==Document::rootId()&&!root.children.empty()&&root.children.back()==id&&!root.locked&&root.visible&&n.visible&&!n.locked&&n.opacity==1&&n.blend==BlendMode::Normal&&n.transform==LayerTransform{}&&n.maskId.empty()&&n.sourceResource.empty()&&n.content.empty()&&n.objectId.empty()&&n.sourceKind==SourceKind::Reconstructed;}
}
Result<unsigned> desktopMyPaintStrokeCount(const DocumentSnapshot& s) try {
    auto valid=validateSnapshot(s);if(!valid)return valid.error();unsigned count=0;
    for(const auto& [id,node]:*s.nodes)if(croppedRaster::isStage(node.stage)){
        auto v=croppedRaster::parse(s,id);if(!v)return v.error();const auto& b=*s.resources->at(v.value().manifest).bytes;
        const auto m=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(b.data()),int(b.size())));
        if(m.isObject()&&m.object()["desktop_protocol"].toString()==Marker)++count;
    }
    return count;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mypaint.count-allocation");}
Result<void> validateMyPaintRuntime(const MyPaintRuntime& r) try {
#ifndef Q_OS_LINUX
    (void)r;return error(ErrorCode::UnsupportedFeature,"mypaint.linux-runtime-only");
#else
    need(QFileInfo(r.python).isAbsolute()&&QFileInfo(r.python).isExecutable()&&QFileInfo(r.worker).isAbsolute()&&QFileInfo(r.pins).isAbsolute(),"mypaint.operator-path");
    const auto root=QFileInfo(r.worker).absolutePath();const auto p=pins(r);
    // System library symlinks are expected; hash their actual bytes, not their names.
    const std::array<std::pair<QString,QString>,4> checks{{{"libmypaint","/lib/x86_64-linux-gnu/libmypaint-1.5.so.1"},{"json_c","/lib/x86_64-linux-gnu/libjson-c.so.5"},{"preset",root+"/mypaint_data/pointy_ink.myb"},{"abi",root+"/mypaint_data/abi15.py"}}};
    for(const auto& [key,path]:checks){QFile f(path);need(f.open(QIODevice::ReadOnly)&&f.size()>0&&f.size()<=8*1024*1024,"mypaint.runtime-missing",ErrorCode::UnsupportedFeature);need(hash(f.readAll())==p[key].toString(),"mypaint.runtime-pin-mismatch",ErrorCode::UnsupportedFeature);}
    (void)read(r.worker,256*1024);return {};
#endif
}catch(const Failure& f){return f.error;}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mypaint.runtime-allocation");}
Result<MyPaintRasterCandidate> buildMyPaintRasterCommand(const DocumentSnapshot& s,Revision revision,
    const MyPaintGesture& input,NodeId layer,NodeId folder,const MyPaintRuntime& runtime,std::stop_token stop) try {
    const auto checkStop=[&]{need(!stop.stop_requested(),"mypaint.cancelled",ErrorCode::Cancelled);};checkStop();
    need(s.revision==revision,"mypaint.revision",ErrorCode::StaleRevision);
    auto valid=validateSnapshot(s);if(!valid)return valid.error();auto count=desktopMyPaintStrokeCount(s);if(!count)return count.error();need(count.value()<32,"mypaint.desktop-32-stroke-limit",ErrorCode::BudgetExceeded);
    const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
    need(s.spec.colorSpec.format==PixelFormat{ColorMode::RGB,Depth::U8,true}&&s.spec.colorSpec.profileId.empty(),"mypaint.rgb8-unprofiled-only",ErrorCode::UnsupportedFeature);
    need(w&&h&&w<=8192&&h<=8192&&w*h<=20000000,"mypaint.canvas",ErrorCode::BudgetExceeded);
    need(!layer.empty()&&!folder.empty()&&layer!=folder&&!s.nodes->contains(layer),"mypaint.layer-id");
    const bool newFolder=!s.nodes->contains(folder);need(newFolder||eligible(s,folder),"mypaint.folder-not-top-neutral");
    const auto& rootNode=s.nodes->at(Document::rootId());
    need(rootNode.visible&&!rootNode.locked&&rootNode.opacity==1&&rootNode.blend==BlendMode::Normal&&rootNode.transform==LayerTransform{}&&rootNode.maskId.empty(),"mypaint.root-not-neutral");
    need(input.samples.size()>=2&&input.samples.size()<=1024,"mypaint.sample-count",ErrorCode::BudgetExceeded);
    need(input.color.activeChannels==3&&input.color.profileId.empty()&&unit(input.color.alpha)&&unit(input.opacity)&&input.opacity*input.color.alpha>0,"mypaint.color");for(auto c:input.color.channels)need(unit(c),"mypaint.color");
    need(std::isfinite(input.radiusScale)&&input.radiusScale>=.25&&input.radiusScale<=8,"mypaint.radius-scale");need(input.preset=="pointy_ink"||input.preset=="dry_brush","mypaint.preset");
    QJsonArray points;auto start=input.samples.front().timestampUs,last=start;bool tiltIgnored=false;
    for(const auto& p:input.samples){checkStop();need(std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<w&&p.y<h&&unit(p.pressure)&&p.timestampUs>=last&&p.timestampUs-start<=60000000,"mypaint.sample-bounds");last=p.timestampUs;tiltIgnored|=p.tiltX.has_value()||p.tiltY.has_value();points.append(QJsonObject{{"x",p.x},{"y",p.y},{"pressure",input.mouseSimulated?1.0:p.pressure},{"time_ms",double(p.timestampUs-start)/1000.}});}
    auto available=validateMyPaintRuntime(runtime);if(!available)return available.error();checkStop();
    auto selectedPins=pins(runtime);const auto root=QFileInfo(runtime.worker).absolutePath();
    if(input.preset=="dry_brush"){const auto dry=hash(read(root+"/mypaint_data/dry_brush.myb",256*1024));need(dry=="57786e87241e977e90873ab1d5a05bdf0228a299cda41b328b97ff01fccff6af","mypaint.dry-pin");selectedPins["preset"]=dry;}
    const auto workerHash=hash(read(runtime.worker,256*1024));QJsonObject helpers;for(const auto* name:{"crop_plan.py","dab_guard.py"})helpers[name]=hash(read(root+"/"+name,256*1024));
    QString color="#";for(unsigned c=0;c<3;++c)color+=QString("%1").arg(int(std::lround(input.color.channels[c]*255)),2,16,QChar('0'));color=color.toUpper();
    QJsonObject body{{"type","draw_stroke"},{"brush","mypaint_ink"},{"expected_revision",QString::number(revision)},{"points",points},{"preset",input.preset},{"paint_mode","normal"},{"color",color},{"radius_scale",input.radiusScale}};
    QJsonObject state{{"width",double(w)},{"height",double(h)},{"revision",QString::number(revision)},{"document_id",idText(s.documentId)}};
    QTemporaryDir directory;need(directory.isValid(),"mypaint.temp-directory",ErrorCode::IoError);
    const auto request=directory.filePath("request-global.json"),pngPath=directory.filePath("stroke.png"),manifestPath=directory.filePath("crop-manifest.json");
    QFile requestFile(request);need(requestFile.open(QIODevice::WriteOnly|QIODevice::NewOnly),"mypaint.request-file",ErrorCode::IoError);
    const auto requestBytes=QJsonDocument(QJsonObject{{"body",body},{"state",state},{"pins",selectedPins},{"worker_sha256",workerHash},{"helper_sha256",helpers}}).toJson(QJsonDocument::Compact);need(requestFile.write(requestBytes)==requestBytes.size(),"mypaint.request-write",ErrorCode::IoError);requestFile.close();
    QProcess process;auto env=QProcessEnvironment::systemEnvironment();env.insert("DRAWBORAD_MYPAINT_PINS_FILE",runtime.pins);env.insert("OMP_NUM_THREADS","1");process.setProcessEnvironment(env);
    QElapsedTimer timer;timer.start();process.start(runtime.python,{runtime.worker,"crop",request,pngPath,manifestPath});
    struct Kill {QProcess& p;~Kill(){if(p.state()!=QProcess::NotRunning){p.kill();p.waitForFinished(3000);}}} guard{process};
    while(process.state()==QProcess::Starting){checkStop();need(timer.elapsed()<3000,"mypaint.start-timeout",ErrorCode::Timeout);process.waitForStarted(20);}
    need(process.state()!=QProcess::NotRunning,"mypaint.start",ErrorCode::UnsupportedFeature);QByteArray stdoutData,stderrData;
    while(process.state()!=QProcess::NotRunning){checkStop();need(timer.elapsed()<15000,"mypaint.timeout",ErrorCode::Timeout);process.waitForFinished(20);stdoutData+=process.readAllStandardOutput();stderrData+=process.readAllStandardError();need(stdoutData.size()<=16384&&stderrData.size()<=65536,"mypaint.process-output",ErrorCode::BudgetExceeded);}
    stdoutData+=process.readAllStandardOutput();stderrData+=process.readAllStandardError();checkStop();need(stdoutData.size()<=16384&&stderrData.size()<=65536,"mypaint.process-output",ErrorCode::BudgetExceeded);need(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"mypaint.worker-rejected",ErrorCode::UnsupportedFeature);
    const auto receipt=json(stdoutData);const auto png=read(pngPath,16*1024*1024),manifestBytes=read(manifestPath,1024*1024);auto manifest=json(manifestBytes);
    need(receipt["png_sha256"].toString()==hash(png)&&receipt["manifest_sha256"].toString()==hash(manifestBytes),"mypaint.receipt-digest",ErrorCode::DigestMismatch);
    need(manifest["prototype"].toString()=="mypaint-cropped-study-v1","mypaint.prototype");
    const auto worker=manifest["worker_manifest"].toObject();need(worker["pins"].toObject()==selectedPins&&worker["worker_sha256"].toString()==workerHash&&manifest["helper_sha256"].toObject()==helpers,"mypaint.output-pins",ErrorCode::DigestMismatch);
    need(manifest["full_canvas"].toArray()==QJsonArray{double(w),double(h)}&&manifest["canvas_resized"]==QJsonValue(false)&&manifest["full_canvas_rgba_allocated"]==QJsonValue(false)&&manifest["original_global_points"].toArray()==points,"mypaint.output-input-binding");
    const auto origin=manifest["origin"].toArray(),size=manifest["roi_size"].toArray();need(origin.size()==2&&size.size()==2,"mypaint.roi-schema");
    auto integer=[](QJsonValue v){const auto n=v.toDouble(-1);need(v.isDouble()&&std::isfinite(n)&&n>=0&&n<=8192&&std::floor(n)==n,"mypaint.roi-number");return std::uint64_t(n);};
    Rect roi{std::int64_t(integer(origin[0])),std::int64_t(integer(origin[1])),integer(size[0]),integer(size[1])};need(croppedRaster::fits(roi,w,h)&&roi.width<=2048&&roi.height<=2048&&roi.width*roi.height<=1048576,"mypaint.roi-budget",ErrorCode::BudgetExceeded);
    const auto locals=manifest["translated_local_points"].toArray();need(locals.size()==points.size()&&worker["points"].toArray()==locals&&worker["canvas"].toArray()==size&&worker["document_id"].toString()==idText(s.documentId)&&worker["paint_mode"].toString()=="normal"&&worker["color"].toString()==color&&worker["preset"].toString()==input.preset,"mypaint.worker-inputs");
    need((input.radiusScale==1&&worker["radius_scale"].isUndefined())||(worker["radius_scale"].isDouble()&&worker["radius_scale"].toDouble()==input.radiusScale),"mypaint.radius-binding");
    for(qsizetype i=0;i<points.size();++i){const auto p=points[i].toObject(),l=locals[i].toObject();need(l["x"].isDouble()&&l["y"].isDouble()&&l["x"].toDouble(-1)>=0&&l["y"].toDouble(-1)>=0&&l["x"].toDouble(-1)<roi.width&&l["y"].toDouble(-1)<roi.height&&l["x"].toDouble(-1)==p["x"].toDouble()-roi.x&&l["y"].toDouble(-1)==p["y"].toDouble()-roi.y&&l["pressure"]==p["pressure"]&&l["time_ms"]==p["time_ms"],"mypaint.point-translation");}
    const auto dab=worker["actual_dab_guard"].toObject();const auto dabs=dab["checked_dabs"].toDouble(-1);need(dab["passed"]==QJsonValue(true)&&std::isfinite(dabs)&&dabs>=0&&dabs<=10000000&&std::floor(dabs)==dabs,"mypaint.guard-proof");
    need(manifest["png_sha256"].toString()==hash(png),"mypaint.png-digest",ErrorCode::DigestMismatch);
    InputInfo info;info.format=ImageFormat::Png;DecodeOptions options;options.limits.decodedBytes=4*1048576;
    const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(png.constData()),std::size_t(png.size()));auto decoded=decodeRasterBytes(bytes,info,options);if(!decoded)return decoded.error();auto& pixels=decoded.value();
    need(pixels.width==roi.width&&pixels.height==roi.height&&pixels.rowStride==roi.width*4&&pixels.format==PixelFormat{ColorMode::RGB,Depth::U8,true}&&info.icc.empty()&&info.iccProfile.empty()&&pixels.profileId.empty(),"mypaint.png-layout");need(QString::fromStdString(sha256(pixels.bytes).hex())==worker["rgba_sha256"].toString(),"mypaint.rgba-digest",ErrorCode::DigestMismatch);
    bool visible=false;for(std::size_t i=0;i<pixels.bytes.size();i+=4){visible|=pixels.bytes[i+3]!=0;for(unsigned c=0;c<3;++c)need(pixels.bytes[i+c]==std::uint8_t(std::lround(input.color.channels[c]*255)),"mypaint.pixel-color");}need(visible,"mypaint.empty");checkStop();
    manifest["desktop_protocol"]=Marker;manifest["mouse_pressure_simulated"]=input.mouseSimulated;manifest["tilt_ignored"]=tiltIgnored;manifest["desktop_opacity"]=input.opacity*input.color.alpha;
    const auto stored=QJsonDocument(manifest).toJson(QJsonDocument::Compact);auto command=croppedRaster::makeNormal(std::move(pixels.bytes),Bytes(png.begin(),png.end()),Bytes(stored.begin(),stored.end()),roi,w,h,layer);if(!command)return command.error();
    std::uint64_t resourceBytes=0;for(const auto& [id,r]:*s.resources){(void)id;resourceBytes+=r.bytes->size();}for(const auto& op:command.value().operations)if(const auto* r=std::get_if<InstallResource>(&op))if(!s.resources->contains(r->resource.id))resourceBytes+=r->resource.bytes->size();need(resourceBytes<=128ULL*1024*1024,"mypaint.document-resource-budget",ErrorCode::BudgetExceeded);
    if(newFolder){LayerNode n;n.id=folder;n.parent=Document::rootId();n.kind=NodeKind::Group;n.stage=FolderStage;n.name="MyPaint strokes (up to 32)";command.value().operations.insert(command.value().operations.begin(),AddNode{n});}
    for(auto& op:command.value().operations)if(auto* n=std::get_if<AddNode>(&op))if(n->node.id==layer){n->node.parent=folder;n->node.name="MyPaint "+input.preset.toStdString()+" (baked stroke)";n->node.opacity=input.opacity*input.color.alpha;}
    auto candidate=Document::fromSnapshot(s);if(!candidate)return candidate.error();auto applied=candidate.value().apply(command.value(),revision);if(!applied)return applied.error();auto mergedPlan=planMergedDocument(candidate.value().snapshot());if(!mergedPlan)return mergedPlan.error();checkStop();
    return MyPaintRasterCandidate{revision,std::move(command.value()),roi,layer,folder};
}catch(const Failure& f){return f.error;}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mypaint.allocation");}
}
