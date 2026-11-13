#include "apps/studio/QtBridge.h"
#include "ui/WorkflowController.h"
#include <QFile>
#include <QDir>
#include "platform/CanvasItem.h"
#include "platform/CanvasFrame.h"
#include "brush/HollowInkCommand.h"
#include "core/LineartRaster.h"
#include "storage/ProjectStore.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QSGRendererInterface>
#include <cassert>
#include <iostream>
using namespace drawborad;
void settlePreview(CanvasItem& c){QTest::qWait(50);QElapsedTimer t;t.start();while(c.previewing()&&t.elapsed()<15000)QTest::qWait(5);assert(!c.previewing());QTest::qWait(20);}
void settle(CanvasItem& c){QElapsedTimer t;t.start();while(c.rendering()&&t.elapsed()<15000)QTest::qWait(5);assert(!c.rendering());QTest::qWait(20);}
void settleFill(QtBridge& b){QElapsedTimer t;t.start();while(b.fillBusy()&&t.elapsed()<30000)QTest::qWait(5);assert(!b.fillBusy());}
int main(int argc,char**argv){
 QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);QGuiApplication app(argc,argv);
 const auto connectionSource=qEnvironmentVariable("DRAWBORAD_HOLLOW_CONNECTION_SOURCE");
 if(!connectionSource.isEmpty()){
  QImage source(connectionSource);assert(!source.isNull());QtBridge bridge;bridge.createDocument(source.width(),source.height(),300,true);assert(bridge.importLineartUrl(QUrl::fromLocalFile(connectionSource)));const auto original=*bridge.canvasSnapshot();
  const auto x=qEnvironmentVariableIntValue("DRAWBORAD_HOLLOW_X"),y=qEnvironmentVariableIntValue("DRAWBORAD_HOLLOW_Y");assert(bridge.fillLineartAt(x,y,Qt::white,true));settleFill(bridge);assert(bridge.fillPending());assert(bridge.acceptFill());const auto hollowed=*bridge.canvasSnapshot();
  assert(bridge.fillLineartAt(x,y,QColor("#151419")));settleFill(bridge);assert(bridge.fillPending());assert(bridge.canvasSnapshot()->sameContent(hollowed));assert(bridge.acceptFill());const auto colored=*bridge.canvasSnapshot();
  const auto destination=qEnvironmentVariable("DRAWBORAD_HOLLOW_OUTPUT");assert(!destination.isEmpty()&&QDir().mkpath(destination));const auto png=destination+"/controlled-fill.png";assert(!QFile::exists(png));auto frame=renderCanvasFrame(colored,UINT64_MAX);assert(frame&&frame.value().save(png));const auto project=destination+"/controlled-fill.draw";assert(!QFile::exists(project));bridge.saveUrl(QUrl::fromLocalFile(project));assert(bridge.undoDrawing());assert(bridge.canvasSnapshot()->sameContent(hollowed));bridge.openUrl(QUrl::fromLocalFile(project));assert(bridge.canvasSnapshot()->sameContent(colored));
  assert(bridge.fillLineartAt(x,y,QColor("#151419")));settleFill(bridge);assert(bridge.fillPending());bridge.cancelFill();assert(bridge.canvasSnapshot()->sameContent(colored));for(const auto&[id,res]:*original.resources)assert(colored.resources->at(id).bytes==res.bytes);std::cout<<"PASS real old-source hollow->derived independent fill, pending/accept/undo/reopen/review-cancel, original resources unchanged\n";return 0;
 }
 const auto hollowSource=qEnvironmentVariable("DRAWBORAD_HOLLOW_SOURCE");
 if(!hollowSource.isEmpty()){
  QImage source(hollowSource);assert(!source.isNull());source=source.convertToFormat(QImage::Format_RGBA8888);const auto w=source.width(),h=source.height();Bytes raw(std::size_t(w)*h*4);std::vector<Rgba8> pixels(std::size_t(w)*h);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){const auto c=source.pixelColor(x,y);assert(c.alpha()==255&&c.red()==c.green()&&c.green()==c.blue()&&(c.red()==0||c.red()==255));const auto i=std::size_t(y)*w+x;pixels[i]={std::uint8_t(c.red()),std::uint8_t(c.green()),std::uint8_t(c.blue()),255};for(unsigned ch=0;ch<4;ch++)raw[i*4+ch]=source.constScanLine(y)[x*4+ch];}
  QFile encoded(hollowSource);assert(encoded.open(QIODevice::ReadOnly));auto bytes=encoded.readAll();NewDocumentRequest request;request.dimensions={double(w),double(h),Unit::Px,300};request.transparent=false;request.paperColor.channels={1,1,1,0};request.paperColor.alpha=1;auto spec=validateDocument(request,{});assert(spec);auto made=Document::create(spec.value(),Id::fromNumber(1));assert(made);auto& doc=made.value();
  auto ink=Id::fromNumber(2),overlay=Id::fromNumber(3);auto add=makeLineartRasterCommand(raw,Bytes(bytes.begin(),bytes.end()),w,h,ink);assert(add&&doc.apply(add.value(),0));const auto original=doc.snapshot();
  const auto sx=qEnvironmentVariableIntValue("DRAWBORAD_HOLLOW_X"),sy=qEnvironmentVariableIntValue("DRAWBORAD_HOLLOW_Y");assert(sx>=0&&sx<w&&sy>=0&&sy<h&&pixels[std::size_t(sy)*w+sx].r==0);auto selected=floodFill(pixels,w,h,sx,sy,0);assert(selected);
  auto hollow=hollowInkOverlayCommand(original,ink,overlay,Id::fromNumber(4),selected.value(),1);assert(hollow&&doc.apply(hollow.value(),original.revision));const auto changed=doc.snapshot();
  const auto destination=qEnvironmentVariable("DRAWBORAD_HOLLOW_OUTPUT");assert(!destination.isEmpty()&&QDir().mkpath(destination));const auto file=destination+"/selected-hollow.png";assert(!QFile::exists(file));auto frame=renderCanvasFrame(changed,UINT64_MAX);assert(frame&&frame.value().save(file));
  assert(doc.undo());assert(doc.snapshot().sameContent(original));assert(doc.redo());assert(doc.snapshot().sameContent(changed));ProjectStore store;const auto project=(destination+"/selected-hollow.draw").toStdString();assert(store.save(project,changed));auto reopened=store.recover(project);assert(reopened&&reopened.value().snapshot.sameContent(changed));for(const auto&[id,res]:*original.resources)assert(changed.resources->at(id).bytes==res.bytes);
  std::uint64_t cleared=0;for(const auto& op:hollow.value().operations)if(const auto* paint=std::get_if<Paint>(&op))for(std::size_t i=0;i<paint->event.geometry.size();i+=2)cleared+=paint->event.geometry[i+1].x-paint->event.geometry[i].x;
  std::cout<<"selected="<<selected.value().pixelCount<<" cleared="<<cleared<<" preserved="<<selected.value().pixelCount-cleared<<" size="<<w<<"x"<<h<<" undo-redo-reopen=exact source-resources=unchanged\n";return 0;
 }
 const auto privateSource=qEnvironmentVariable("DRAWBORAD_REGION_REVIEW_SOURCE");
 if(!privateSource.isEmpty()){
  QImage source(privateSource);assert(!source.isNull());QtBridge inspected;inspected.createDocument(source.width(),source.height(),300);
  assert(inspected.importLineartUrl(QUrl::fromLocalFile(privateSource)));const auto original=*inspected.canvasSnapshot();
  assert(inspected.fillLineartAt(qEnvironmentVariableIntValue("DRAWBORAD_REGION_REVIEW_X"),qEnvironmentVariableIntValue("DRAWBORAD_REGION_REVIEW_Y"),QColor("#151419")));settleFill(inspected);assert(inspected.fillPending());assert(inspected.canvasSnapshot()->sameContent(original));
  auto frame=renderCanvasFrame(*inspected.displaySnapshot(),UINT64_MAX);assert(frame);const auto output=qEnvironmentVariable("DRAWBORAD_REGION_REVIEW_OUTPUT");assert(!output.isEmpty());assert(!QFile::exists(output));assert(frame.value().save(output));std::cout<<inspected.fillNotice().toStdString()<<"\n";
  inspected.cancelFill();assert(inspected.canvasSnapshot()->sameContent(original));return 0;
 }

 NewDocumentRequest r;r.dimensions={128,128,Unit::Px,300};auto spec=validateDocument(r,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(1));assert(d);
 Bytes rgba(128*128*4,0);for(int y=20;y<100;y++)for(int x=20;x<100;x++){rgba[(y*128+x)*4]=255;rgba[(y*128+x)*4+3]=255;}
 auto cmd=makeLineartRasterCommand(rgba,{137,80,78,71,13,10,26,10},128,128,Id::fromNumber(2));assert(cmd);assert(d.value().apply(cmd.value(),0));
 QTemporaryDir dir;assert(dir.isValid());auto path=dir.filePath("raster.draw");ProjectStore st;assert(st.save(path.toStdString(),d.value().snapshot()));
 QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(path));assert(bridge.canvasSnapshot());
 QQuickWindow w;w.resize(320,320);CanvasItem canvas(w.contentItem());canvas.setSize({320,320});canvas.setProperty("bridge",QVariant::fromValue<QObject*>(&bridge));w.show();QTest::qWait(300);
 settle(canvas);const auto image=w.grabWindow();assert(!image.isNull());const auto c=image.pixelColor(160,160);assert(c.red()>250&&c.green()<5&&c.blue()<5);assert(canvas.renderError().isEmpty());
 assert(bridge.canvasSnapshot()->sameContent(d.value().snapshot()));
 assert(bridge.addLayer());settle(canvas);const auto count=bridge.canvasSnapshot()->eventIndex->size();
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(75,275));QTest::mouseMove(&w,QPoint(150,275));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(225,275));QTest::qWait(100);settle(canvas);
 assert(bridge.canvasSnapshot()->eventIndex->size()==count+1);int nativeDark=255;auto nativeShot=w.grabWindow();for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++)nativeDark=std::min(nativeDark,nativeShot.pixelColor(150+dx,275+dy).red());assert(nativeDark<120);
 const auto painted=*bridge.canvasSnapshot();canvas.zoomAt(2,160,160);canvas.panBy(10,0);QTest::qWait(100);settle(canvas);assert(bridge.canvasSnapshot()->sameContent(painted));
 canvas.fitDocument();assert(canvas.configureBrush(10,QColor("#00ccff"),.35,.4,.2));const auto beforePreview=*bridge.canvasSnapshot();auto cleanShot=w.grabWindow();
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(60,25));QTest::mouseMove(&w,QPoint(250,25));settlePreview(canvas);assert(canvas.strokePreviewActive());assert(bridge.canvasSnapshot()->sameContent(beforePreview));auto provisional=w.grabWindow();assert(provisional.pixelColor(150,25)!=cleanShot.pixelColor(150,25));
 QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(250,25));settle(canvas);assert(!canvas.strokePreviewActive());auto committed=w.grabWindow();assert(provisional.copy(20,0,280,45)==committed.copy(20,0,280,45));assert(bridge.undoDrawing());settle(canvas);assert(bridge.canvasSnapshot()->sameContent(beforePreview));
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(60,25));QTest::mouseMove(&w,QPoint(250,25));settlePreview(canvas);QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(&canvas,&escape);QTest::qWait(80);assert(!canvas.strokePreviewActive());assert(bridge.canvasSnapshot()->sameContent(beforePreview));assert(w.grabWindow().copy(20,0,280,45)==cleanShot.copy(20,0,280,45));
 canvas.fitDocument();assert(canvas.configureBrush(4,QColor("#0000ff"),.6,1,.2));
 assert(!canvas.configureBrush(-1,QColor("#0000ff"),.6,1,.2));assert(canvas.brushWidth()==4);
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(50,35));QTest::mouseMove(&w,QPoint(150,35));assert(canvas.configureBrush(40,QColor("#ff0000"),1,.2,0));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(250,35));settle(canvas);
 const auto& expressive=bridge.canvasSnapshot()->eventIndex->back();assert(expressive.style.width==4&&expressive.style.opacity==.6&&expressive.style.hardness==1);assert(expressive.color.channels[2]>.99&&expressive.color.channels[0]<.01);assert(expressive.geometry.front().pressure<.06&&expressive.geometry.back().pressure<.06);assert(std::any_of(expressive.geometry.begin(),expressive.geometry.end(),[](const auto& p){return p.pressure>.9;}));auto blue=w.grabWindow().pixelColor(150,35);assert(blue.blue()>blue.red());
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(50,300));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(250,300));settle(canvas);const auto& nextStyle=bridge.canvasSnapshot()->eventIndex->back();assert(nextStyle.style.width==40&&nextStyle.color.channels[0]>.99&&nextStyle.color.channels[2]<.01);assert(nextStyle.geometry.front().pressure==1&&nextStyle.geometry.back().pressure==1);
 assert(canvas.configureBrush(6,QColor("#202020"),1,.8,.4));QPointingDevice pen("synthetic pen",42,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
 QTabletEvent penDown(QEvent::TabletPress,&pen,{50,70},{50,70},.4,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);penDown.setTimestamp(100);QCoreApplication::sendEvent(&canvas,&penDown);
 const auto beforePenRelease=*bridge.canvasSnapshot();QTabletEvent penMove(QEvent::TabletMove,&pen,{200,70},{200,70},.6,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::LeftButton);penMove.setTimestamp(110);QCoreApplication::sendEvent(&canvas,&penMove);settlePreview(canvas);assert(canvas.strokePreviewActive());assert(bridge.canvasSnapshot()->sameContent(beforePenRelease));
 QTabletEvent penUp(QEvent::TabletRelease,&pen,{250,70},{250,70},.7,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::NoButton);penUp.setTimestamp(120);QCoreApplication::sendEvent(&canvas,&penUp);settle(canvas);const auto& penStroke=bridge.canvasSnapshot()->eventIndex->back();assert(std::abs(penStroke.geometry.front().pressure-.4)<.001&&std::abs(penStroke.geometry.back().pressure-.7)<.001);assert(penStroke.startUs==100000&&penStroke.endUs==120000);
 const auto beforeErase=*bridge.canvasSnapshot();auto beforeEraseShot=w.grabWindow();assert(canvas.configureBrush(12,QColor("#202020"),.5,.4,.4));QPointingDevice eraserDevice("synthetic eraser",43,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
 QTabletEvent eraseDown(QEvent::TabletPress,&eraserDevice,{50,70},{50,70},.8,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);eraseDown.setTimestamp(200);QCoreApplication::sendEvent(&canvas,&eraseDown);
 QTabletEvent eraseMove(QEvent::TabletMove,&eraserDevice,{250,70},{250,70},.8,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::LeftButton);eraseMove.setTimestamp(220);QCoreApplication::sendEvent(&canvas,&eraseMove);settlePreview(canvas);assert(bridge.canvasSnapshot()->sameContent(beforeErase));auto erasePreview=w.grabWindow();assert(erasePreview.pixelColor(150,70).red()>=beforeEraseShot.pixelColor(150,70).red());
 QTabletEvent eraseUp(QEvent::TabletRelease,&eraserDevice,{250,70},{250,70},.8,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::NoButton);eraseUp.setTimestamp(225);QCoreApplication::sendEvent(&canvas,&eraseUp);settle(canvas);assert(erasePreview.copy(30,55,240,30)==w.grabWindow().copy(30,55,240,30));assert(bridge.undoDrawing());settle(canvas);assert(bridge.canvasSnapshot()->sameContent(beforeErase));
 const auto beforeBad=*bridge.canvasSnapshot();auto beforeBadShot=w.grabWindow();QTabletEvent badDown(QEvent::TabletPress,&pen,{50,90},{50,90},.6,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);badDown.setTimestamp(500);QCoreApplication::sendEvent(&canvas,&badDown);QTabletEvent badMove(QEvent::TabletMove,&pen,{250,90},{250,90},.6,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::LeftButton);badMove.setTimestamp(510);QCoreApplication::sendEvent(&canvas,&badMove);settlePreview(canvas);
 QTabletEvent badUp(QEvent::TabletRelease,&pen,{250,90},{250,90},.6,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::NoButton);badUp.setTimestamp(450);QCoreApplication::sendEvent(&canvas,&badUp);QTest::qWait(100);assert(!canvas.strokePreviewActive());assert(bridge.canvasSnapshot()->sameContent(beforeBad));assert(canvas.brushNotice().contains("brush.sample"));assert(w.grabWindow().copy(30,80,240,20)==beforeBadShot.copy(30,80,240,20));
 const auto styledSnapshot=*bridge.canvasSnapshot();auto styledPath=dir.filePath("styled.draw");bridge.saveUrl(QUrl::fromLocalFile(styledPath));bridge.openUrl(QUrl::fromLocalFile(styledPath));settle(canvas);assert(bridge.canvasSnapshot()->sameContent(styledSnapshot));
 auto unsupported=d.value().snapshot();auto nodes=std::make_shared<NodeMap>(*unsupported.nodes);nodes->at(Id::fromNumber(2)).transform.affine[4]=1;unsupported.nodes=nodes;
 auto unsupportedPath=dir.filePath("transformed-raster.draw");assert(st.save(unsupportedPath.toStdString(),unsupported));bridge.openUrl(QUrl::fromLocalFile(unsupportedPath));QTest::qWait(100);settle(canvas);assert(!canvas.renderError().isEmpty());
 assert(bridge.addLayer());settle(canvas);const auto beforeErrorDraw=*bridge.canvasSnapshot();assert(!canvas.renderError().isEmpty());
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(75,275));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(225,275));assert(bridge.canvasSnapshot()->sameContent(beforeErrorDraw));
 qmlRegisterType<CanvasItem>("Drawborad.Native",1,0,"NativeCanvas");
 QtBridge uiBridge;QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&uiBridge);
 engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));assert(!engine.rootObjects().isEmpty());
 auto uiWindow=qobject_cast<QQuickWindow*>(engine.rootObjects().front());assert(uiWindow);QTest::qWait(200);
 auto uiCanvas=uiWindow->findChild<CanvasItem*>("nativeCanvas");auto errorLabel=uiWindow->findChild<QObject*>("canvasRenderError");assert(uiCanvas&&errorLabel);auto widthControl=uiWindow->findChild<QObject*>("brushWidthSlider");assert(widthControl);assert(widthControl->setProperty("value",9));assert(QMetaObject::invokeMethod(widthControl,"moved"));assert(uiCanvas->brushWidth()==9);assert(uiCanvas->configureBrush(12,QColor::fromRgbF(.08,.08,.08),1,.8,0));
 uiBridge.openUrl(QUrl::fromLocalFile(unsupportedPath));QTest::qWait(200);settle(*uiCanvas);assert(!uiCanvas->renderError().isEmpty());assert(errorLabel->property("visible").toBool());assert(errorLabel->property("text").toString().contains(uiCanvas->renderError()));
 uiBridge.openUrl(QUrl::fromLocalFile(path));QTest::qWait(200);settle(*uiCanvas);assert(uiCanvas->renderError().isEmpty());assert(!errorLabel->property("visible").toBool());
 QElapsedTimer phase;phase.start();auto mark=[&](const char* name){std::cout<<"poster phase "<<name<<": "<<phase.restart()<<" ms"<<std::endl;};
 NewDocumentRequest posterRequest;posterRequest.dimensions={3300,5100,Unit::Px,300};auto posterSpec=validateDocument(posterRequest,{});assert(posterSpec);auto poster=Document::create(posterSpec.value(),Id::fromNumber(11));assert(poster);
 Bytes posterPixels(3300*5100*4,0);for(int y=2000;y<3100;y++)for(int x=1100;x<2200;x++){posterPixels[(y*3300+x)*4]=255;posterPixels[(y*3300+x)*4+3]=255;}
 auto posterCmd=makeLineartRasterCommand(posterPixels,{137,80,78,71,13,10,26,10},3300,5100,Id::fromNumber(12));assert(posterCmd);assert(poster.value().apply(posterCmd.value(),0));posterPixels.clear();posterPixels.shrink_to_fit();
 mark("fixture construction");auto posterPath=dir.filePath("poster.draw");assert(st.save(posterPath.toStdString(),poster.value().snapshot()));mark("initial save");uiBridge.openUrl(QUrl::fromLocalFile(posterPath));mark("open returns with queued composite");assert(uiCanvas->rendering());assert(uiWindow->findChild<QObject*>("canvasRendering")->property("visible").toBool());
 int heartbeats=0;QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(uiCanvas->rendering())++heartbeats;});heartbeat.start(1);settle(*uiCanvas);heartbeat.stop();assert(heartbeats>0);mark("composite wait with live event loop");QTest::qWait(300);assert(uiCanvas->renderError().isEmpty());assert(!errorLabel->property("visible").toBool());
 auto center=uiCanvas->mapToScene(uiCanvas->documentRect().center()).toPoint();auto posterShot=uiWindow->grabWindow();assert(!posterShot.isNull());auto posterColor=posterShot.pixelColor(center);assert(posterColor.red()>250&&posterColor.green()<5);
 for(int dy=-50;dy<=50;dy++)for(int dx=-50;dx<=50;dx++){auto px=posterShot.pixelColor(center+QPoint(dx,dy));assert(px.red()>250&&px.green()<5&&px.blue()<5);}
 mark("display and screenshot");assert(uiBridge.addLayer());assert(uiCanvas->rendering());auto whileBusy=*uiBridge.canvasSnapshot();QTest::mousePress(uiWindow,Qt::LeftButton,{},center);QTest::mouseRelease(uiWindow,Qt::LeftButton,{},center+QPoint(10,0));assert(uiBridge.canvasSnapshot()->sameContent(whileBusy));settle(*uiCanvas);mark("add empty layer");auto posterEvents=uiBridge.canvasSnapshot()->eventIndex->size();auto rect=uiCanvas->documentRect();auto start=uiCanvas->mapToScene({rect.left()+rect.width()*0.2,rect.top()+rect.height()*0.8}).toPoint();auto end=uiCanvas->mapToScene({rect.left()+rect.width()*0.8,rect.top()+rect.height()*0.8}).toPoint();
 mark("pointer setup");QTest::mousePress(uiWindow,Qt::LeftButton,{},start);QTest::mouseMove(uiWindow,end);settlePreview(*uiCanvas);assert(uiWindow->findChild<QObject*>("strokePreviewStatus")->property("visible").toBool());assert(uiBridge.canvasSnapshot()->eventIndex->size()==posterEvents);auto posterPreview=uiWindow->grabWindow();QTest::mouseRelease(uiWindow,Qt::LeftButton,{},end);mark("mouse stroke and composite");QTest::qWait(200);settle(*uiCanvas);assert(uiBridge.canvasSnapshot()->eventIndex->size()==posterEvents+1);assert(uiCanvas->renderError().isEmpty());
 auto paintedShot=uiWindow->grabWindow();auto strokeMid=(start+end)/2;int darkest=255;for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)darkest=std::min(darkest,paintedShot.pixelColor(strokeMid+QPoint(dx,dy)).red());assert(darkest<120);assert(posterPreview.copy(strokeMid.x()-25,strokeMid.y()-5,50,10)==paintedShot.copy(strokeMid.x()-25,strokeMid.y()-5,50,10));
 const auto posterPainted=*uiBridge.canvasSnapshot();auto savedPoster=dir.filePath("poster-painted.draw");mark("stroke screenshot");uiBridge.saveUrl(QUrl::fromLocalFile(savedPoster));mark("painted save");uiBridge.openUrl(QUrl::fromLocalFile(savedPoster));mark("painted reopen with queued composite");QTest::qWait(200);settle(*uiCanvas);settle(*uiCanvas);assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));assert(uiCanvas->renderError().isEmpty());
 mark("reopened screenshot wait");for(int i=0;i<4;i++){uiCanvas->panBy(i%2?10:-10,0);assert(!uiWindow->grabWindow().isNull());}mark("four pan plus screenshots");assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 QTest::mousePress(uiWindow,Qt::LeftButton,{},start);uiBridge.setReplayCount(0);QTest::mouseRelease(uiWindow,Qt::LeftButton,{},end);assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 uiBridge.setReplayCount(0);uiBridge.setReplayCount(uiBridge.eventCount());settle(*uiCanvas);assert(uiCanvas->renderError().isEmpty());assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));auto latestShot=uiWindow->grabWindow();int latestDark=255;for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)latestDark=std::min(latestDark,latestShot.pixelColor(strokeMid+QPoint(dx,dy)).red());assert(latestDark<120);
 {auto temporary=std::make_unique<CanvasItem>();temporary->setSize({320,320});temporary->setProperty("bridge",QVariant::fromValue<QObject*>(&uiBridge));assert(temporary->rendering());QTest::qWait(20);}QTest::qWait(20);
 auto workflow=qobject_cast<WorkflowController*>(uiBridge.workflow());assert(workflow);assert(uiWindow->findChild<QObject*>("nativeExportButton"));
 const auto exportDir=qEnvironmentVariable("DRAWBORAD_TEST_EXPORT_DIR",dir.path());assert(QDir().mkpath(exportDir));auto pngPath=exportDir+"/poster-export.png";auto oraPath=exportDir+"/poster-export.ora";
 assert(workflow->exportNative(QUrl::fromLocalFile(pngPath),"png"));assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 QImage png(pngPath);assert(png.width()==3300&&png.height()==5100);assert(std::abs(png.dotsPerMeterX()*0.0254-300)<0.1);assert(png.pixelColor(1650,2550).red()>250&&png.pixelColor(1650,2550).green()<5);
 assert(workflow->exportNative(QUrl::fromLocalFile(oraPath),"ora"));QFile ora(oraPath);assert(ora.open(QIODevice::ReadOnly));auto oraBytes=ora.readAll();ora.close();assert(oraBytes.contains("xres=\"300\"")&&oraBytes.contains("w=\"3300\"")&&oraBytes.contains("h=\"5100\""));assert(oraBytes.count("<layer ")==2);
 assert(workflow->message().contains("RGB")&&workflow->message().contains("CMYK"));assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 assert(!workflow->exportPsd(QUrl::fromLocalFile(exportDir+"/unsupported-poster.psd")));assert(!QFile::exists(exportDir+"/unsupported-poster.psd"));assert(workflow->message().contains("PNG /"));
 assert(!workflow->exportNative(QUrl::fromLocalFile(oraPath),"ora"));assert(ora.open(QIODevice::ReadOnly));assert(ora.readAll()==oraBytes);assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 // Desktop source-only fill starts from an empty document, embeds original PNG,
 // and keeps fills independent instead of sampling a colored composite.
 QtBridge fillBridge;fillBridge.createDocument(64,64,300);auto emptyFill=*fillBridge.canvasSnapshot();
 QImage line(64,64,QImage::Format_RGBA8888);line.fill(Qt::white);
 for(int y=4;y<60;y++)for(int x=4;x<60;x++)if(x==4||x==32||x==59||y==4||y==59)line.setPixelColor(x,y,Qt::black);
 auto linePath=dir.filePath("line.png");assert(line.save(linePath));
 assert(fillBridge.importLineartUrl(QUrl::fromLocalFile(linePath)));auto embedded=*fillBridge.canvasSnapshot();assert(embedded.nodes->size()==3);
 assert(fillBridge.fillLineartAt(10,10,QColor("#ff0000")));settleFill(fillBridge);assert(fillBridge.fillPending());assert(fillBridge.canvasSnapshot()->sameContent(embedded));assert(fillBridge.acceptFill());auto filled=*fillBridge.canvasSnapshot();
 for(const auto&[hash,res]:*embedded.resources)assert(*filled.resources->at(hash).bytes==*res.bytes);
 assert(fillBridge.fillLineartAt(10,10,QColor("#0000ff")));settleFill(fillBridge);assert(fillBridge.acceptFill());assert(fillBridge.undoDrawing());assert(fillBridge.canvasSnapshot()->sameContent(filled));assert(fillBridge.undoDrawing());assert(fillBridge.canvasSnapshot()->sameContent(embedded));
 assert(fillBridge.fillLineartAt(4,4,Qt::red));settleFill(fillBridge);assert(fillBridge.canvasSnapshot()->sameContent(embedded));
 assert(fillBridge.undoDrawing());assert(fillBridge.canvasSnapshot()->sameContent(emptyFill));assert(fillBridge.redoDrawing());assert(fillBridge.canvasSnapshot()->sameContent(embedded));
 assert(!fillBridge.importLineartUrl(QUrl::fromLocalFile(linePath)));assert(fillBridge.canvasSnapshot()->sameContent(embedded));
 CanvasItem fillCanvas(uiWindow->contentItem());fillCanvas.setWidth(256);fillCanvas.setHeight(256);fillCanvas.setProperty("bridge",QVariant::fromValue(static_cast<QObject*>(&fillBridge)));settle(fillCanvas);fillCanvas.setBucketTool(true);
 auto fillRect=fillCanvas.documentRect();QPointF fillPoint=fillRect.topLeft()+QPointF(fillRect.width()*10/64,fillRect.height()*10/64);
 QMouseEvent fillPress(QEvent::MouseButtonPress,fillPoint,fillPoint,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(&fillCanvas,&fillPress);settleFill(fillBridge);settle(fillCanvas);assert(fillBridge.eventCount()==0);assert(fillBridge.fillPending());assert(fillBridge.displaySnapshot()->eventIndex->size()==1);assert(!fillBridge.addLayer());assert(!fillBridge.setLayerVisible(fillBridge.activeLayerId(),false));assert(fillBridge.canvasSnapshot()->sameContent(embedded));auto pendingWorkflow=qobject_cast<WorkflowController*>(fillBridge.workflow());assert(!pendingWorkflow->exportNative(QUrl::fromLocalFile(dir.filePath("pending-export.png")),"png"));assert(!QFile::exists(dir.filePath("pending-export.png")));
 auto reviewFrame=uiWindow->grabWindow();fillBridge.cancelFill();settle(fillCanvas);assert(!fillBridge.fillPending());assert(fillBridge.canvasSnapshot()->sameContent(embedded));
 QCoreApplication::sendEvent(&fillCanvas,&fillPress);settleFill(fillBridge);settle(fillCanvas);assert(fillBridge.acceptFill());settle(fillCanvas);assert(uiWindow->grabWindow().copy(fillRect.toRect())==reviewFrame.copy(fillRect.toRect()));assert(fillBridge.eventCount()==1);assert(!fillCanvas.strokePreviewActive());
 QtBridge customFill;customFill.createDocument(64,64,300);assert(customFill.addLayer());auto customBefore=*customFill.canvasSnapshot();assert(!customFill.importLineartUrl(QUrl::fromLocalFile(linePath)));assert(customFill.canvasSnapshot()->sameContent(customBefore));
 QtBridge largeFill;largeFill.createDocument(1725,2625,300);auto largeBefore=*largeFill.canvasSnapshot();assert(!largeFill.importLineartUrl(QUrl::fromLocalFile(linePath)));assert(largeFill.canvasSnapshot()->sameContent(largeBefore));
 for(const QSize nativeSize:{QSize(1725,2625),QSize(3300,5100)}){
  QtBridge big;big.createDocument(nativeSize.width(),nativeSize.height(),300);
  QImage native(nativeSize,QImage::Format_RGBA8888);native.fill(Qt::white);
  for(int y=0;y<native.height();y++)native.setPixelColor(native.width()/2,y,Qt::black);
  const auto nativePath=dir.filePath(QString("line-%1.png").arg(native.width()));assert(native.save(nativePath));
  assert(big.importLineartUrl(QUrl::fromLocalFile(nativePath)));const auto original=*big.canvasSnapshot();
  int beats=0;QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(big.fillBusy())++beats;});heartbeat.start();
  assert(big.fillLineartAt(10,10,Qt::red));assert(big.fillBusy());big.setReplayCount(0);assert(!big.replaying());const auto forbidden=dir.filePath("pending-must-not-save.draw");big.saveUrl(QUrl::fromLocalFile(forbidden));assert(!QFile::exists(forbidden));
  QTest::qWait(5);big.cancelFill();settleFill(big);assert(big.canvasSnapshot()->sameContent(original));
  assert(big.fillLineartAt(10,10,Qt::red));assert(!big.fillLineartAt(20,20,Qt::blue));settleFill(big);heartbeat.stop();assert(beats>0);assert(big.fillPending());assert(big.canvasSnapshot()->sameContent(original));big.saveUrl(QUrl::fromLocalFile(forbidden));assert(!QFile::exists(forbidden));assert(big.acceptFill());assert(big.eventCount()==1);
  const auto completed=*big.canvasSnapshot();for(const auto&[hash,res]:*original.resources)assert(*completed.resources->at(hash).bytes==*res.bytes);
  for(const auto& p:completed.eventIndex->back().geometry)assert(p.x<=native.width()/2 && p.y<native.height());
  const auto safe=dir.filePath(QString("large-fill-%1.draw").arg(native.width()));big.saveUrl(QUrl::fromLocalFile(safe));assert(QFile::exists(safe));assert(big.undoDrawing());assert(big.canvasSnapshot()->sameContent(original));big.openUrl(QUrl::fromLocalFile(safe));assert(big.canvasSnapshot()->sameContent(completed));
 }
 assert(uiWindow->findChild<QObject*>("lineartBucketTool"));assert(uiWindow->findChild<QObject*>("importLineartButton"));
 // Actual Main.qml hollow-tool selection -> candidate -> accept/cancel path.
 auto whiteChoice=uiWindow->findChild<QObject*>("documentWhitePaper");assert(whiteChoice&&whiteChoice->setProperty("checked",true));assert(uiWindow->findChild<QObject*>("documentWidth")->setProperty("text","64"));assert(uiWindow->findChild<QObject*>("documentHeight")->setProperty("text","64"));assert(QMetaObject::invokeMethod(uiWindow->findChild<QObject*>("newDocumentDialog"),"accepted"));assert(!uiBridge.canvasSnapshot()->spec.background.transparent);QImage inkImage(64,64,QImage::Format_RGBA8888);inkImage.fill(Qt::white);
 for(int y=10;y<40;y++)for(int x=10;x<40;x++)inkImage.setPixelColor(x,y,Qt::black);
 for(int x=45;x<60;x++)inkImage.setPixelColor(x,20,Qt::black);
 auto inkFile=dir.filePath("qml-hollow-source.png");assert(inkImage.save(inkFile));assert(uiBridge.importLineartUrl(QUrl::fromLocalFile(inkFile)));auto hollowOriginal=*uiBridge.canvasSnapshot();
 auto hollowCanvas=uiWindow->findChild<CanvasItem*>("nativeCanvas");assert(hollowCanvas);settle(*hollowCanvas);
 auto hollowToggle=uiWindow->findChild<QObject*>("hollowInkTool");assert(hollowToggle);assert(hollowToggle->setProperty("checked",true));assert(QMetaObject::invokeMethod(hollowToggle,"toggled"));assert(hollowCanvas->hollowTool()&&!hollowCanvas->bucketTool());
 const auto hollowRect=hollowCanvas->documentRect();const auto point=hollowRect.topLeft()+QPointF(hollowRect.width()*20/64,hollowRect.height()*20/64);
 QMouseEvent hollowPress(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(hollowCanvas,&hollowPress);settleFill(uiBridge);settle(*hollowCanvas);assert(uiBridge.fillPending());assert(uiBridge.canvasSnapshot()->sameContent(hollowOriginal));
 auto reviewStatus=uiWindow->findChild<QObject*>("fillPreviewStatus");assert(reviewStatus&&reviewStatus->property("visible").toBool());auto cancelButton=uiWindow->findChild<QObject*>("cancelLineartFill");assert(cancelButton&&QMetaObject::invokeMethod(cancelButton,"clicked"));settle(*hollowCanvas);assert(!uiBridge.fillPending()&&uiBridge.canvasSnapshot()->sameContent(hollowOriginal));
 QCoreApplication::sendEvent(hollowCanvas,&hollowPress);settleFill(uiBridge);settle(*hollowCanvas);auto candidate=*uiBridge.displaySnapshot();auto candidateFrame=renderCanvasFrame(candidate,UINT64_MAX);assert(candidateFrame);assert(candidateFrame.value().pixelColor(20,20)==QColor(Qt::white));assert(candidateFrame.value().pixelColor(10,20)==QColor(Qt::black));assert(candidateFrame.value().pixelColor(50,20)==QColor(Qt::black));
 auto acceptButton=uiWindow->findChild<QObject*>("acceptLineartFill");assert(acceptButton&&acceptButton->property("enabled").toBool()&&QMetaObject::invokeMethod(acceptButton,"clicked"));settle(*hollowCanvas);auto hollowDone=*uiBridge.canvasSnapshot();assert(hollowDone.sameContent(candidate));assert(uiBridge.eventCount()==1);for(const auto&[id,res]:*hollowOriginal.resources)assert(hollowDone.resources->at(id).bytes==res.bytes);
 assert(uiBridge.fillLineartAt(20,20,Qt::white,true));settleFill(uiBridge);assert(!uiBridge.fillPending()&&uiBridge.canvasSnapshot()->sameContent(hollowDone));assert(uiBridge.fillNotice().contains("撤销"));
 auto hollowProject=dir.filePath("qml-hollow.draw");uiBridge.saveUrl(QUrl::fromLocalFile(hollowProject));assert(uiBridge.undoDrawing());assert(uiBridge.canvasSnapshot()->sameContent(hollowOriginal));assert(uiBridge.fillLineartAt(50,20,Qt::white,true));settleFill(uiBridge);assert(!uiBridge.fillPending()&&uiBridge.canvasSnapshot()->sameContent(hollowOriginal));assert(uiBridge.fillNotice().contains("细笔触"));uiBridge.openUrl(QUrl::fromLocalFile(hollowProject));assert(uiBridge.canvasSnapshot()->sameContent(hollowDone));
 // Reopened accepted hollow derives a fill reference without sampling old colors.
 settle(*hollowCanvas);auto bucketToggle=uiWindow->findChild<QObject*>("lineartBucketTool");assert(bucketToggle&&bucketToggle->setProperty("checked",true));assert(QMetaObject::invokeMethod(bucketToggle,"toggled"));assert(hollowCanvas->bucketTool()&&!hollowCanvas->hollowTool());
 assert(hollowCanvas->configureBrush(4,QColor("#c02020"),1,1,0));QCoreApplication::sendEvent(hollowCanvas,&hollowPress);settleFill(uiBridge);settle(*hollowCanvas);assert(uiBridge.fillPending()&&uiBridge.canvasSnapshot()->sameContent(hollowDone));assert(QMetaObject::invokeMethod(acceptButton,"clicked"));settle(*hollowCanvas);auto hollowColored=*uiBridge.canvasSnapshot();assert(hollowColored.nodes->at(Document::rootId()).children.size()==4&&uiBridge.eventCount()==2);assert(hollowColored.eventIndex->front()==hollowDone.eventIndex->front());
 auto coloredFrame=renderCanvasFrame(hollowColored,UINT64_MAX);assert(coloredFrame&&coloredFrame.value().pixelColor(20,20)==QColor("#c02020"));assert(coloredFrame.value().pixelColor(10,20)==QColor(Qt::black));assert(coloredFrame.value().pixelColor(50,20)==QColor(Qt::black));for(const auto&[id,res]:*hollowOriginal.resources)assert(*hollowColored.resources->at(id).bytes==*res.bytes);
 auto coloredProject=dir.filePath("qml-hollow-colored.draw");uiBridge.saveUrl(QUrl::fromLocalFile(coloredProject));assert(uiBridge.undoDrawing());assert(uiBridge.canvasSnapshot()->sameContent(hollowDone));assert(uiBridge.redoDrawing());assert(uiBridge.canvasSnapshot()->sameContent(hollowColored));uiBridge.openUrl(QUrl::fromLocalFile(coloredProject));assert(uiBridge.canvasSnapshot()->sameContent(hollowColored));
 assert(uiBridge.fillLineartAt(20,20,QColor("#2030d0")));settleFill(uiBridge);assert(uiBridge.fillPending());assert(uiBridge.acceptFill());const auto blueDocument=*uiBridge.canvasSnapshot();assert(blueDocument.eventIndex->size()==3);const auto& firstFill=blueDocument.eventIndex->at(1).geometry;const auto& secondFill=blueDocument.eventIndex->at(2).geometry;assert(firstFill.size()==secondFill.size());for(std::size_t i=0;i<firstFill.size();i++)assert(firstFill[i].x==secondFill[i].x&&firstFill[i].y==secondFill[i].y);auto blueFrame=renderCanvasFrame(blueDocument,UINT64_MAX);assert(blueFrame&&blueFrame.value().pixelColor(20,20)==QColor("#2030d0"));assert(uiBridge.undoDrawing());assert(uiBridge.canvasSnapshot()->sameContent(hollowColored));
 std::cout<<"PASS real Qt software/QML: imported pixels, native paint, pan/zoom, visible error recovery; 3300x5100 seam-free display, paint, save/reopen\n";
}
