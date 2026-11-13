#include "platform/MyPaintRasterCommand.h"
#include "core/MyPaintCroppedRaster.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace drawborad;
static Document document(){NewDocumentRequest r;r.dimensions={700,360,Unit::Px,300};r.transparent=false;r.paperColor.channels={1,1,1,0};auto spec=validateDocument(r,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(90));assert(d);return std::move(d.value());}
static MyPaintGesture gesture(){MyPaintGesture g;g.color.channels={.15,.2,.3,0};g.color.alpha=.8;g.opacity=.7;for(unsigned i=0;i<=100;++i){InputSample p;p.x=100+i*5;p.y=170+25*std::sin(i/9.);p.pressure=.1+.8*std::sin(3.141592653589793*i/100);p.timestampUs=9000000+i*12000;p.tiltX=12.;g.samples.push_back(p);}return g;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
#ifdef DRAWBORAD_SOURCE_DIR
 const auto root=QStringLiteral(DRAWBORAD_SOURCE_DIR);
#else
 const auto root=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath("../..");
#endif
 MyPaintRuntime runtime{QStringLiteral("/usr/bin/python3"),root+"/cloud/mypaint_worker.py",root+"/cloud/mypaint_data/pins.json"};
 auto availability=validateMyPaintRuntime(runtime);if(!availability){std::cerr<<availability.error().messageKey<<'\n';return 1;}
 auto doc=document();const auto before=doc.snapshot();auto g=gesture();
 auto candidate=buildMyPaintRasterCommand(before,before.revision,g,Id::fromNumber(2),Id::fromNumber(3),runtime);
 if(!candidate){std::cerr<<candidate.error().messageKey<<'\n';return 1;}
 assert(doc.snapshot().sameContent(before));assert(doc.apply(candidate.value().command,candidate.value().baseRevision));const auto accepted=doc.snapshot();
 assert(accepted.nodes->at(Id::fromNumber(2)).parent==Id::fromNumber(3));assert(accepted.nodes->at(Id::fromNumber(2)).opacity==.8*.7);
 auto count=desktopMyPaintStrokeCount(accepted);assert(count&&count.value()==1);auto view=croppedRaster::parse(accepted,Id::fromNumber(2));assert(view);
 const auto& mb=*accepted.resources->at(view.value().manifest).bytes;
 const auto meta=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(mb.data()),int(mb.size()))).object();
 assert(meta["tilt_ignored"].toBool()&&!meta["mouse_pressure_simulated"].toBool());const auto points=meta["original_global_points"].toArray();assert(points.size()==101);
 for(int i=0;i<points.size();++i){auto p=points[i].toObject();assert(p["pressure"].toDouble()==g.samples[std::size_t(i)].pressure&&p["time_ms"].toDouble()==i*12.);}
 assert(doc.undo()&&doc.snapshot().sameContent(before));assert(doc.redo()&&doc.snapshot().sameContent(accepted));
 QTemporaryDir dir;assert(dir.isValid());ProjectStore store;const auto file=(dir.path()+"/stroke.draw").toStdString();assert(store.save(file,accepted));auto reopened=store.recover(file);assert(reopened&&reopened.value().snapshot.sameContent(accepted));
 auto merged=renderMergedDocument(accepted,{accepted.revision});assert(merged);bool changed=false;for(std::size_t i=0;i<merged.value().merged.bytes.size();i+=4)changed|=merged.value().merged.bytes[i]<255;assert(changed);
 auto mouse=g;mouse.mouseSimulated=true;mouse.preset="dry_brush";
 const auto continued=doc.snapshot();auto second=buildMyPaintRasterCommand(continued,continued.revision,mouse,Id::fromNumber(4),Id::fromNumber(3),runtime);if(!second){std::cerr<<second.error().messageKey<<'\n';return 1;}assert(doc.apply(second.value().command,second.value().baseRevision));
 auto mv=croppedRaster::parse(doc.snapshot(),Id::fromNumber(4));assert(mv);const auto& mouseBytes=*doc.snapshot().resources->at(mv.value().manifest).bytes;const auto mm=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(mouseBytes.data()),int(mouseBytes.size()))).object();assert(mm["mouse_pressure_simulated"].toBool());for(const auto& p:mm["original_global_points"].toArray())assert(p.toObject()["pressure"].toDouble()==1.);
 // Reuse immutable resources to exercise the document cap without rendering 32 strokes.
 for(unsigned i=0;i<30;++i){auto n=accepted.nodes->at(Id::fromNumber(2));n.id=Id::fromNumber(100+i);assert(doc.apply(Command{AddNode{n}},doc.snapshot().revision));}
 assert(desktopMyPaintStrokeCount(doc.snapshot()).value()==32);
 auto capped=buildMyPaintRasterCommand(doc.snapshot(),doc.snapshot().revision,g,Id::fromNumber(200),Id::fromNumber(3),runtime);assert(!capped&&capped.error().code==ErrorCode::BudgetExceeded&&capped.error().messageKey=="mypaint.desktop-32-stroke-limit");
 // Existing crop art without desktop marker does not consume the stroke count.
 auto plain=Document::fromSnapshot(before);assert(plain);auto unmarked=meta;unmarked.remove("desktop_protocol");auto raw=QJsonDocument(unmarked).toJson(QJsonDocument::Compact);
 auto old=croppedRaster::makeNormal(Bytes(view.value().rgba.begin(),view.value().rgba.end()),*accepted.resources->at(view.value().png).bytes,Bytes(raw.begin(),raw.end()),view.value().bounds,700,360,Id::fromNumber(10));assert(old&&plain.value().apply(old.value(),0));assert(desktopMyPaintStrokeCount(plain.value().snapshot()).value()==0);
 std::stop_source stop;stop.request_stop();auto cancelled=buildMyPaintRasterCommand(before,0,g,Id::fromNumber(2),Id::fromNumber(3),runtime,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 std::stop_source during;std::jthread requester([&]{std::this_thread::sleep_for(std::chrono::milliseconds(60));during.request_stop();});auto interrupted=buildMyPaintRasterCommand(before,0,g,Id::fromNumber(2),Id::fromNumber(3),runtime,during.get_token());requester.join();assert(!interrupted&&interrupted.error().code==ErrorCode::Cancelled);
 auto invalid=g;invalid.samples[1].pressure=NAN;assert(!buildMyPaintRasterCommand(before,0,invalid,Id::fromNumber(2),Id::fromNumber(3),runtime));invalid=g;invalid.samples[1].timestampUs=0;assert(!buildMyPaintRasterCommand(before,0,invalid,Id::fromNumber(2),Id::fromNumber(3),runtime));
 assert(!buildMyPaintRasterCommand(before,1,g,Id::fromNumber(2),Id::fromNumber(3),runtime));
 const auto saved=doc.snapshot();assert(!doc.apply(candidate.value().command,0));assert(doc.snapshot().sameContent(saved));
 QFile pinCopy(dir.path()+"/pins.json");assert(pinCopy.open(QIODevice::WriteOnly));pinCopy.write("{\"libmypaint\":\"bad\"}");pinCopy.close();auto badRuntime=runtime;badRuntime.pins=pinCopy.fileName();assert(!validateMyPaintRuntime(badRuntime));
 std::cout<<"PASS real pinned MyPaint pointy/dry -> native crop candidate, pressure/time binding, mouse simulation, Undo/save/recover/merge,32 tagged strokes only, cancel and invalid guards\n";
}
