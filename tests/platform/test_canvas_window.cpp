#include "apps/studio/QtBridge.h"
#include "platform/CanvasItem.h"
#include "core/LineartRaster.h"
#include "storage/ProjectStore.h"
#include <QGuiApplication>
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
 std::cout<<"PASS real Qt software window: imported pixels, native paint, pan/zoom unchanged, unsupported raster error prevents drawing\n";
}
