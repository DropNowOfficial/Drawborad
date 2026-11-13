#include "apps/studio/QtBridge.h"
#include "platform/CanvasItem.h"
#include "core/LineartRaster.h"
#include "storage/ProjectStore.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QSGRendererInterface>
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(int argc,char**argv){
 QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);QGuiApplication app(argc,argv);
 NewDocumentRequest r;r.dimensions={128,128,Unit::Px,300};auto spec=validateDocument(r,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(1));assert(d);
 Bytes rgba(128*128*4,0);for(int y=20;y<100;y++)for(int x=20;x<100;x++){rgba[(y*128+x)*4]=255;rgba[(y*128+x)*4+3]=255;}
 auto cmd=makeLineartRasterCommand(rgba,{137,80,78,71,13,10,26,10},128,128,Id::fromNumber(2));assert(cmd);assert(d.value().apply(cmd.value(),0));
 QTemporaryDir dir;assert(dir.isValid());auto path=dir.filePath("raster.draw");ProjectStore st;assert(st.save(path.toStdString(),d.value().snapshot()));
 QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(path));assert(bridge.canvasSnapshot());
 QQuickWindow w;w.resize(320,320);CanvasItem canvas(w.contentItem());canvas.setSize({320,320});canvas.setProperty("bridge",QVariant::fromValue<QObject*>(&bridge));w.show();QTest::qWait(300);
 const auto image=w.grabWindow();assert(!image.isNull());const auto c=image.pixelColor(160,160);assert(c.red()>250&&c.green()<5&&c.blue()<5);assert(canvas.renderError().isEmpty());
 assert(bridge.canvasSnapshot()->sameContent(d.value().snapshot()));
 assert(bridge.addLayer());const auto count=bridge.canvasSnapshot()->eventIndex->size();
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(75,275));QTest::mouseMove(&w,QPoint(150,275));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(225,275));QTest::qWait(100);
 assert(bridge.canvasSnapshot()->eventIndex->size()==count+1);assert(w.grabWindow().pixelColor(150,275).red()<50);
 const auto painted=*bridge.canvasSnapshot();canvas.zoomAt(2,160,160);canvas.panBy(10,0);QTest::qWait(100);assert(bridge.canvasSnapshot()->sameContent(painted));
 auto unsupported=d.value().snapshot();auto nodes=std::make_shared<NodeMap>(*unsupported.nodes);nodes->at(Id::fromNumber(2)).transform.affine[4]=1;unsupported.nodes=nodes;
 auto unsupportedPath=dir.filePath("transformed-raster.draw");assert(st.save(unsupportedPath.toStdString(),unsupported));bridge.openUrl(QUrl::fromLocalFile(unsupportedPath));QTest::qWait(100);assert(!canvas.renderError().isEmpty());
 assert(bridge.addLayer());const auto beforeErrorDraw=*bridge.canvasSnapshot();assert(!canvas.renderError().isEmpty());
 QTest::mousePress(&w,Qt::LeftButton,{},QPoint(75,275));QTest::mouseRelease(&w,Qt::LeftButton,{},QPoint(225,275));assert(bridge.canvasSnapshot()->sameContent(beforeErrorDraw));
 qmlRegisterType<CanvasItem>("Drawborad.Native",1,0,"NativeCanvas");
 QtBridge uiBridge;QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&uiBridge);
 engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));assert(!engine.rootObjects().isEmpty());
 auto uiWindow=qobject_cast<QQuickWindow*>(engine.rootObjects().front());assert(uiWindow);QTest::qWait(200);
 auto uiCanvas=uiWindow->findChild<CanvasItem*>("nativeCanvas");auto errorLabel=uiWindow->findChild<QObject*>("canvasRenderError");assert(uiCanvas&&errorLabel);
 uiBridge.openUrl(QUrl::fromLocalFile(unsupportedPath));QTest::qWait(200);assert(!uiCanvas->renderError().isEmpty());assert(errorLabel->property("visible").toBool());assert(errorLabel->property("text").toString().contains(uiCanvas->renderError()));
 uiBridge.openUrl(QUrl::fromLocalFile(path));QTest::qWait(200);assert(uiCanvas->renderError().isEmpty());assert(!errorLabel->property("visible").toBool());
 QElapsedTimer phase;phase.start();auto mark=[&](const char* name){std::cout<<"poster phase "<<name<<": "<<phase.restart()<<" ms"<<std::endl;};
 NewDocumentRequest posterRequest;posterRequest.dimensions={3300,5100,Unit::Px,300};auto posterSpec=validateDocument(posterRequest,{});assert(posterSpec);auto poster=Document::create(posterSpec.value(),Id::fromNumber(11));assert(poster);
 Bytes posterPixels(3300*5100*4,0);for(int y=2000;y<3100;y++)for(int x=1100;x<2200;x++){posterPixels[(y*3300+x)*4]=255;posterPixels[(y*3300+x)*4+3]=255;}
 auto posterCmd=makeLineartRasterCommand(posterPixels,{137,80,78,71,13,10,26,10},3300,5100,Id::fromNumber(12));assert(posterCmd);assert(poster.value().apply(posterCmd.value(),0));posterPixels.clear();posterPixels.shrink_to_fit();
 mark("fixture construction");auto posterPath=dir.filePath("poster.draw");assert(st.save(posterPath.toStdString(),poster.value().snapshot()));mark("initial save");uiBridge.openUrl(QUrl::fromLocalFile(posterPath));mark("open and composite");QTest::qWait(300);assert(uiCanvas->renderError().isEmpty());assert(!errorLabel->property("visible").toBool());
 auto center=uiCanvas->mapToScene(uiCanvas->documentRect().center()).toPoint();auto posterShot=uiWindow->grabWindow();assert(!posterShot.isNull());auto posterColor=posterShot.pixelColor(center);assert(posterColor.red()>250&&posterColor.green()<5);
 for(int dy=-50;dy<=50;dy++)for(int dx=-50;dx<=50;dx++){auto px=posterShot.pixelColor(center+QPoint(dx,dy));assert(px.red()>250&&px.green()<5&&px.blue()<5);}
 mark("display and screenshot");assert(uiBridge.addLayer());mark("add empty layer");auto posterEvents=uiBridge.canvasSnapshot()->eventIndex->size();auto rect=uiCanvas->documentRect();auto start=uiCanvas->mapToScene({rect.left()+rect.width()*0.2,rect.top()+rect.height()*0.8}).toPoint();auto end=uiCanvas->mapToScene({rect.left()+rect.width()*0.8,rect.top()+rect.height()*0.8}).toPoint();
 mark("pointer setup");QTest::mousePress(uiWindow,Qt::LeftButton,{},start);QTest::mouseMove(uiWindow,end);QTest::mouseRelease(uiWindow,Qt::LeftButton,{},end);mark("mouse stroke and composite");QTest::qWait(200);assert(uiBridge.canvasSnapshot()->eventIndex->size()==posterEvents+1);assert(uiCanvas->renderError().isEmpty());
 auto paintedShot=uiWindow->grabWindow();auto strokeMid=(start+end)/2;int darkest=255;for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)darkest=std::min(darkest,paintedShot.pixelColor(strokeMid+QPoint(dx,dy)).red());assert(darkest<120);
 const auto posterPainted=*uiBridge.canvasSnapshot();auto savedPoster=dir.filePath("poster-painted.draw");mark("stroke screenshot");uiBridge.saveUrl(QUrl::fromLocalFile(savedPoster));mark("painted save");uiBridge.openUrl(QUrl::fromLocalFile(savedPoster));mark("painted reopen and composite");QTest::qWait(200);assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));assert(uiCanvas->renderError().isEmpty());
 mark("reopened screenshot wait");for(int i=0;i<4;i++){uiCanvas->panBy(i%2?10:-10,0);assert(!uiWindow->grabWindow().isNull());}mark("four pan plus screenshots");assert(uiBridge.canvasSnapshot()->sameContent(posterPainted));
 std::cout<<"PASS real Qt software/QML: imported pixels, native paint, pan/zoom, visible error recovery; 3300x5100 seam-free display, paint, save/reopen\n";
}
