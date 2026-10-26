#include "apps/studio/QtBridge.h"
#include "ui/WorkflowController.h"
#include "ui/ReviewJob.h"
#include "ui/PaintedDocument.h"
#include "tests/support/RealCoffeeFixture.h"
#include "platform/CanvasItem.h"
#include "view/LayerTree.h"
#include "render/ReferenceRenderer.h"
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
using namespace drawborad;
namespace {
QObject* qmlObject(QObject* root,const char* id){QQmlExpression expression(qmlContext(root),root,QString::fromLatin1(id));return expression.evaluate().value<QObject*>();}
QPoint point(QQuickItem* canvas,const DocumentSnapshot& snapshot,double x,double y){
    const auto scale=std::min(canvas->width()/double(snapshot.spec.pixelSize.width),canvas->height()/double(snapshot.spec.pixelSize.height));
    return canvas->mapToScene({(canvas->width()-double(snapshot.spec.pixelSize.width)*scale)/2+x*scale,
        (canvas->height()-double(snapshot.spec.pixelSize.height)*scale)/2+y*scale}).toPoint();
}
QColor rendered(const DocumentSnapshot& snapshot,NodeId layer,int x,int y){
    ReferenceRenderer renderer;
    auto raw=renderer.renderRegion(snapshot,layer,{x,y,1,1},{snapshot.revision,snapshot.eventIndex->size(),UINT64_MAX});
    if(!raw)return {};
    auto tile=nativeMaskedTile(snapshot,layer,std::move(raw.value()),x,y);if(!tile)return {};
    const auto& b=tile.value().bytes;return QColor(b[0],b[1],b[2],b[3]);
}
bool selectReadyModel(WorkflowController* workflow,const QUrl& package){
    workflow->setModelPackage(package);
    if(!workflow->checkModel())return false;
    QElapsedTimer elapsed;elapsed.start();
    while(workflow->busy() && elapsed.elapsed()<120000)QTest::qWait(25);
    return !workflow->busy() && workflow->modelReady();
}
}
class RealImageReviewTests : public QObject {
    Q_OBJECT
  private slots:
    void rejectedAnalysisPreservesPendingReview_data(){
        QTest::addColumn<bool>("changeDocument");
        QTest::newRow("stale-document")<<true;
        QTest::newRow("source-disappears-after-decode")<<false;
    }
    void rejectedAnalysisPreservesPendingReview(){
        QFETCH(bool,changeDocument);
        QTest::failOnWarning(QRegularExpression(".*"));QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto image=owned.filePath("review-source.png");QVERIFY(QFile::copy(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"),image));
        NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};const auto spec=validateDocument(request,{});QVERIFY(spec);
        auto document=Document::create(spec.value(),Id::fromNumber(6900));QVERIFY(document);
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",image},{"sourceHash","cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7"},{"width",600},{"height",400}});
        const Bytes alpha(600*400,255);Resource mask{sha256(alpha),ResourceKind::Mask,std::make_shared<const Bytes>(alpha),{source.id}};
        LayerNode object;object.id=Id::fromNumber(6901);object.kind=NodeKind::Object;object.parent=Document::rootId();object.maskId=mask.id;object.name="Existing review";
        LayerNode layer;layer.id=Id::fromNumber(6902);layer.parent=object.id;layer.objectId=object.id;layer.name="Paint";
        Command setup;setup.operations={InstallResource{source},InstallResource{mask},AddNode{object},AddNode{layer}};QVERIFY(document.value().apply(setup,0));
        const auto project=owned.filePath("pending-review.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(project.toStdWString()),document.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(project));auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow && workflow->hasReview());
        QVERIFY2(selectReadyModel(workflow,QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE"))),qPrintable(workflow->modelStatus()));
        QVERIFY(workflow->renameCandidate(0,"Keep this pending name"));
        const QImage original(workflow->overlayUrl().toLocalFile());
        QVERIFY(workflow->editRectangle(false,10,10,30,30));
        const auto candidates=workflow->candidates();const QImage edited(workflow->overlayUrl().toLocalFile());const auto preview=workflow->sourceUrl();
        QVERIFY(workflow->startImage(QUrl::fromLocalFile(image)));
        if(changeDocument)QVERIFY(bridge.addLayer());
        else{
            // The first proposal progress arrives only after the worker decoded this file.
            QTRY_VERIFY_WITH_TIMEOUT(workflow->progressTotal()>0,120000);
            QVERIFY(workflow->busy());QVERIFY(QFile::remove(image));
        }
        const auto current=*bridge.canvasSnapshot();
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),600000);
        QVERIFY(bridge.canvasSnapshot()->sameContent(current));QVERIFY(workflow->pending());
        QCOMPARE(workflow->candidates(),candidates);QCOMPARE(workflow->sourceUrl(),preview);
        QCOMPARE(QImage(workflow->overlayUrl().toLocalFile()),edited);
        QVERIFY(workflow->undo());QCOMPARE(QImage(workflow->overlayUrl().toLocalFile()),original);
    }
    void repeatedSmallReviewEditsKeepHistoryBoundedAndAuditUndoBranchExact(){
        QTest::failOnWarning(QRegularExpression(".*"));QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto image=owned.filePath("small.png");QImage pixels(32,32,QImage::Format_RGB32);pixels.fill(Qt::white);QVERIFY(pixels.save(image));QFile file(image);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        NewDocumentRequest request;request.dimensions={32,32,Unit::Px,300};const auto spec=validateDocument(request,{});QVERIFY(spec);auto document=Document::create(spec.value(),Id::fromNumber(5100));QVERIFY(document);
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",image},{"sourceHash",QString::fromStdString(sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),std::size_t(bytes.size()))).hex())},{"width",32},{"height",32}});
        const Bytes alpha(1024,255);Resource mask{sha256(alpha),ResourceKind::Mask,std::make_shared<const Bytes>(alpha),{source.id}};
        LayerNode object;object.id=Id::fromNumber(5101);object.kind=NodeKind::Object;object.parent=Document::rootId();object.maskId=mask.id;object.name="Small reviewed object";object.bounds={0,0,32,32};
        LayerNode layer;layer.id=Id::fromNumber(5102);layer.parent=object.id;layer.objectId=object.id;layer.name="Paint";layer.bounds=object.bounds;
        Command setup;setup.operations={InstallResource{source},InstallResource{mask},AddNode{object},AddNode{layer}};QVERIFY(document.value().apply(setup,0));const auto path=owned.filePath("small.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(path.toStdWString()),document.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(path));auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);QVERIFY(workflow->hasReview());QVERIFY(workflow->selectCandidate(0));
        PROCESS_MEMORY_COUNTERS_EX initial{};QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&initial),sizeof(initial)));
        for(unsigned i=0;i<5000;++i)QVERIFY(workflow->editRectangle((i&1)!=0,0,0,1,1));
        PROCESS_MEMORY_COUNTERS_EX final{};QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&final),sizeof(final)));
        const auto growth=final.PrivateUsage>initial.PrivateUsage?final.PrivateUsage-initial.PrivateUsage:0;
        qInfo()<<"5000 small review edits additional private bytes"<<qulonglong(growth);
        QVERIFY2(growth<64ull*1024*1024,"Small review history must remain linear and below64MiB; mask history itself is only about10MiB");
        QVERIFY(workflow->undo());QVERIFY(workflow->undo());QVERIFY(workflow->editRectangle(true,1,1,3,2));QVERIFY(workflow->accept());
        const auto audit=findJsonResource(*bridge.canvasSnapshot(),"drawborad.review-audit.v1");QVERIFY(audit);const auto operations=audit->value("operations").toArray();QCOMPARE(operations.size(),4999);
        QCOMPARE(operations.last().toObject().value("x").toInteger(),qint64(1));QCOMPARE(operations.last().toObject().value("width").toInteger(),qint64(2));
    }
    void fullReviewedCoffeeSceneReconstructsReplaysEditsAndReopensInActualQml(){
        QTest::failOnWarning(QRegularExpression(".*"));QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto image=owned.filePath("source.png");QVERIFY(QFile::copy(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"),image));
        NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};const auto spec=validateDocument(request,{});QVERIFY(spec);auto document=Document::create(spec.value(),Id::fromNumber(2700));QVERIFY(document);
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",image},{"sourceHash","cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7"},{"width",600},{"height",400}});
        QFile file(QStringLiteral(DRAWBORAD_SOURCE_DIR "/tests/fixtures/coffee-reviewed-scene/review.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto review=QJsonDocument::fromJson(file.readAll()).object();
        Command setup;setup.operations.emplace_back(InstallResource{source});unsigned i=0;
        for(const auto& value:review.value("regions").toArray()){
            auto bytes=fixture::reviewedCoffeeMask(i);Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(std::move(bytes)),{source.id}};
            LayerNode object;object.id=Id::fromNumber(2800+i);object.kind=NodeKind::Object;object.parent=Document::rootId();object.name=value.toObject().value("name").toString().toStdString();object.maskId=mask.id;object.bounds={0,0,600,400};
            LayerNode layer;layer.id=Id::fromNumber(2900+i++);layer.parent=object.id;layer.objectId=object.id;layer.name="Editable paint";layer.bounds=object.bounds;
            setup.operations.emplace_back(InstallResource{mask});setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{layer});
        }
        QVERIFY(document.value().apply(setup,0));const auto project=owned.filePath("full-scene.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(project.toStdWString()),document.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(project));auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);QCOMPARE(workflow->candidates().size(),6);QVERIFY(bridge.canvasSnapshot()->eventIndex->empty());
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&bridge);engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));auto button=window->findChild<QQuickItem*>("reconstructButton");QVERIFY(button);QVERIFY(button->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,{},button->mapToScene({button->width()/2,button->height()/2}).toPoint());QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),90000);
        const auto painted=*bridge.canvasSnapshot();QVERIFY2(painted.eventIndex->size()>9000,qPrintable(workflow->message()));const auto pixels=renderPaintedDocument(painted,{painted.revision});QVERIFY(pixels);
        for(std::size_t p=0;p<240000;++p)QCOMPARE(pixels.value().merged.bytes[p*4+3],std::uint8_t(255));
        auto canvas=window->findChild<QQuickItem*>("nativeCanvas");QVERIFY(canvas);QTRY_VERIFY(canvas->isVisible());QTest::qWait(100);const auto finalFrame=window->grabWindow();QVERIFY(!finalFrame.isNull());
        bridge.setReplayCount(0);QTest::qWait(100);const auto blankFrame=window->grabWindow();QVERIFY(blankFrame!=finalFrame);bridge.setReplayCount(bridge.eventCount());
        const auto object=painted.nodes->at(Document::rootId()).children[4];
        const auto& children=painted.nodes->at(object).children;const auto base=std::find_if(children.begin(),children.end(),[&](NodeId id){return painted.nodes->at(id).stage=="base";});QVERIFY(base!=children.end());
        QVERIFY(bridge.setLayerVisible(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(base->bytes.data()),16).toHex()),false));
        const auto hidden=renderPaintedDocument(*bridge.canvasSnapshot(),{bridge.canvasSnapshot()->revision});QVERIFY(hidden);QVERIFY(hidden.value().merged.bytes!=pixels.value().merged.bytes);QVERIFY(bridge.undoDrawing());
        QVERIFY(workflow->selectCandidate(4));QVERIFY(workflow->refineRectangle(230,120,310,160));QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),90000);QVERIFY2(bridge.canvasSnapshot()->eventIndex->size()>painted.eventIndex->size(),qPrintable(workflow->message()));
        const auto refined=*bridge.canvasSnapshot();QVERIFY(std::equal(painted.eventIndex->begin(),painted.eventIndex->end(),refined.eventIndex->begin()));
        bridge.saveUrl(QUrl::fromLocalFile(project));QVERIFY(!bridge.dirty());QVERIFY(QFile::remove(image));bridge.openUrl(QUrl::fromLocalFile(project));QVERIFY(workflow->sourceUrl().isEmpty());QVERIFY(bridge.canvasSnapshot()->sameContent(refined));
        const auto psd=owned.filePath("full-scene.psd");QVERIFY2(workflow->exportPsd(QUrl::fromLocalFile(psd)),qPrintable(workflow->message()));
        QVERIFY(workflow->relinkSource(QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"))));QCOMPARE(*bridge.canvasSnapshot()->eventIndex,*refined.eventIndex);
        const auto artifactRoot=qEnvironmentVariable("DRAWBORAD_TEST_ARTIFACT_DIR");if(!artifactRoot.isEmpty()){
            const auto artifact=QDir(artifactRoot).filePath("full-scene-"+QString::number(QDateTime::currentMSecsSinceEpoch()));QVERIFY(QDir().mkpath(artifact));
            QVERIFY(finalFrame.save(QDir(artifact).filePath("01-full-scene-strokes-fidelity-not-accepted.png")));QVERIFY(blankFrame.save(QDir(artifact).filePath("02-zero-stroke-replay.png")));
            bridge.saveUrl(QUrl::fromLocalFile(QDir(artifact).filePath("coffee-full-regional-strokes.draw")));QVERIFY(!bridge.dirty());QVERIFY(QFile::copy(psd,QDir(artifact).filePath("coffee-full-regional-layers.psd")));
        }else bridge.saveUrl(QUrl::fromLocalFile(project));
        QVERIFY(!bridge.dirty());QVERIFY(window->close());
    }
    void promptedReviewPreservesExistingPaintAndAcceptsBackground(){
        QTest::failOnWarning(QRegularExpression(".*"));QTemporaryDir owned;QVERIFY(owned.isValid());
        NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};const auto spec=validateDocument(request,{});QVERIFY(spec);auto document=Document::create(spec.value(),Id::fromNumber(1100));QVERIFY(document);
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE")},{"sourceHash","cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7"},{"width",600},{"height",400}});
        Command setup;setup.operations.emplace_back(InstallResource{source});
        for(const auto index:{3,5}){
            auto bytes=fixture::realCoffeeMask(unsigned(index));Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(std::move(bytes)),{source.id}};
            LayerNode object;object.id=Id::fromNumber(std::uint64_t(10+index));object.kind=NodeKind::Object;object.parent=Document::rootId();object.maskId=mask.id;object.name=index==3?"Visible inner cup":"Coffee";
            LayerNode layer;layer.id=Id::fromNumber(std::uint64_t(100+index));layer.parent=object.id;layer.objectId=object.id;layer.name="Existing paint";
            setup.operations.emplace_back(InstallResource{mask});setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{layer});
        }
        QVERIFY(document.value().apply(setup,0));const auto path=owned.filePath("existing.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(path.toStdWString()),document.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(path));auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);QVERIFY(workflow->editingExisting());QVERIFY2(selectReadyModel(workflow,QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE"))),qPrintable(workflow->modelStatus()));
        auto before=*bridge.canvasSnapshot();StrokeEvent ink;ink.id=Id::fromNumber(7000);ink.layerId=*bridge.activeLayer();ink.objectId=before.nodes->at(ink.layerId).objectId;ink.parentRevision=before.revision;ink.engineVersion=2;ink.toolId="marker";ink.color.profileId=before.spec.colorSpec.profileId;ink.startUs=1;ink.endUs=2;ink.geometry={{250,140,1,{},{},1,0,0},{270,140,1,{},{},2,0,0}};QVERIFY(bridge.commitStroke(ink));before=*bridge.canvasSnapshot();
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&bridge);engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto promptButton=window->findChild<QQuickItem*>("promptReviewButton");QVERIFY(promptButton);QVERIFY(promptButton->isEnabled());
        QTRY_COMPARE(qmlObject(window,"analysisSource")->property("status").toInt(),1);auto editor=window->findChild<QQuickItem*>("maskEditor");QVERIFY(editor);
        QVERIFY(qmlObject(window,"maskTool")->setProperty("currentIndex",4));QTest::mouseClick(window,Qt::LeftButton,{},point(editor,before,280,225));
        QVERIFY(qmlObject(window,"maskTool")->setProperty("currentIndex",5));QTest::mouseClick(window,Qt::LeftButton,{},point(editor,before,414,94));QCOMPARE(workflow->promptPoints().size(),2);
        QVERIFY(qmlObject(window,"maskTool")->setProperty("currentIndex",6));QTest::mousePress(window,Qt::LeftButton,{},point(editor,before,170,12));QTest::mouseMove(window,point(editor,before,425,316));QTest::mouseRelease(window,Qt::LeftButton,{},point(editor,before,425,316));
        QTest::mouseClick(window,Qt::LeftButton,{},promptButton->mapToScene({promptButton->width()/2,promptButton->height()/2}).toPoint());QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),90000);
        QCOMPARE(workflow->candidates().size(),5);QVERIFY(bridge.canvasSnapshot()->sameContent(before));QVERIFY(workflow->pending());
        QVERIFY(workflow->selectCandidate(2));QVERIFY(workflow->includeCandidate(2,true));QVERIFY(workflow->includeCandidate(1,false));
        QVERIFY(workflow->subtractIncluded());QVERIFY(workflow->undo());QVERIFY(workflow->subtractIncluded());
        QVERIFY(workflow->includeCandidate(0,false));QVERIFY(workflow->includeCandidate(1,true));QVERIFY(workflow->subtractIncluded());
        QVERIFY(workflow->includeCandidate(0,true));QVERIFY(workflow->addBackgroundCandidate());QCOMPARE(workflow->candidates().size(),6);
        QVERIFY(workflow->includeCandidate(0,true));QVERIFY(workflow->accept());
        const auto accepted=*bridge.canvasSnapshot();QCOMPARE(accepted.eventIndex->size(),before.eventIndex->size());QCOMPARE(accepted.eventIndex->front(),before.eventIndex->front());
        std::size_t objects=0;Bytes covered(240000,0);for(const auto& [id,node]:*accepted.nodes){Q_UNUSED(id);if(node.kind!=NodeKind::Object)continue;++objects;const auto& mask=*accepted.resources->at(node.maskId).bytes;for(std::size_t p=0;p<covered.size();++p)covered[p]=std::max(covered[p],mask[p]);}
        QCOMPARE(objects,std::size_t(4));QVERIFY(std::all_of(covered.begin(),covered.end(),[](auto value){return value==255;}));
        bridge.saveUrl(QUrl::fromLocalFile(path));QVERIFY(!bridge.dirty());bridge.openUrl(QUrl::fromLocalFile(path));QCOMPARE(*bridge.canvasSnapshot()->eventIndex,*accepted.eventIndex);
        QVERIFY(window->close());
    }
    void replayPositionDoesNotLeakIntoNewDocument(){
        QtBridge bridge;bridge.createDocument(64,64,300);
        const auto stroke=[&](std::uint64_t id){
            const auto s=*bridge.canvasSnapshot();StrokeEvent event;event.id=Id::fromNumber(id);event.layerId=*bridge.activeLayer();event.objectId=s.nodes->at(event.layerId).objectId;
            event.parentRevision=s.revision;event.engineVersion=2;event.toolId="marker";event.color.profileId=s.spec.colorSpec.profileId;
            event.startUs=1;event.endUs=2;event.geometry={{10,10,1,{},{},1,0,0},{20,20,1,{},{},2,0,0}};return event;
        };
        QVERIFY(bridge.commitStroke(stroke(9001)));bridge.setReplayCount(0);QCOMPARE(bridge.replayCount(),0);
        bridge.createDocument(64,64,300);QVERIFY(bridge.commitStroke(stroke(9002)));QCOMPARE(bridge.replayCount(),bridge.eventCount());
        QVERIFY(bridge.commitStroke(stroke(9003)));QCOMPARE(bridge.eventCount(),2);
    }
    void reviewedCoffeeReconstructRefineSaveExport(){
        QTest::failOnWarning(QRegularExpression(".*"));QTemporaryDir owned;QVERIFY(owned.isValid());
        NewDocumentRequest request;request.dimensions={600,400,Unit::Px,300};const auto spec=validateDocument(request,{});QVERIFY(spec);
        auto document=Document::create(spec.value(),Id::fromNumber(1000));QVERIFY(document);
        const auto image=owned.filePath("coffee.png");QVERIFY(QFile::copy(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"),image));
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",image},{"sourceHash","cc02f8ca188b167c775a7101b5d767d1e71792cf762c33d6fa15a4599b5a8de7"},{"width",600},{"height",400}});
        Command setup;setup.operations.emplace_back(InstallResource{source});
        for(const auto index:{3,5}){
            auto bytes=fixture::realCoffeeMask(unsigned(index));QCOMPARE(bytes.size(),std::size_t(240000));
            Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(std::move(bytes)),{source.id}};
            LayerNode object;object.id=Id::fromNumber(std::uint64_t(10+index));object.kind=NodeKind::Object;object.parent=Document::rootId();object.maskId=mask.id;object.name=index==3?"Visible cup interior":"Coffee surface";
            LayerNode layer;layer.id=Id::fromNumber(std::uint64_t(100+index));layer.parent=object.id;layer.objectId=object.id;layer.name="Manual paint";layer.stage="paint";
            setup.operations.emplace_back(InstallResource{mask});setup.operations.emplace_back(AddNode{object});setup.operations.emplace_back(AddNode{layer});
        }
        QVERIFY(document.value().apply(setup,0));const auto project=owned.filePath("reviewed.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(project.toStdWString()),document.value().snapshot()));
        // A saved project cannot supply worker instructions or an output path.
        {
            auto suspicious=Document::fromSnapshot(document.value().snapshot());QVERIFY(suspicious);
            const auto injected=jsonResource({{"type","drawborad.reconstruct-request.v1"},{"mode","reconstruct"},{"image",image},{"outputDirectory",owned.path()}});
            QVERIFY(suspicious.value().apply(Command{InstallResource{injected}},suspicious.value().snapshot().revision));
            const auto untrusted=owned.filePath("saved-directive.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(untrusted.toStdWString()),suspicious.value().snapshot()));
            QFile marker(owned.filePath("review.delta"));QVERIFY(marker.open(QIODevice::WriteOnly));QCOMPARE(marker.write("preserve-marker"),qint64(15));marker.close();
            QtBridge guarded;guarded.openUrl(QUrl::fromLocalFile(untrusted));auto controller=qobject_cast<WorkflowController*>(guarded.workflow());QVERIFY(controller);const auto original=*guarded.canvasSnapshot();
            QVERIFY(!controller->startReconstruction());QVERIFY(!controller->busy());QVERIFY(guarded.canvasSnapshot()->sameContent(original));
            QVERIFY(marker.open(QIODevice::ReadOnly));QCOMPARE(marker.readAll(),QByteArray("preserve-marker"));marker.close();
        }
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(project));auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);QVERIFY(workflow->hasReview());
        const auto blank=*bridge.canvasSnapshot();QVERIFY(blank.eventIndex->empty());
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto button=window->findChild<QQuickItem*>("reconstructButton");QVERIFY(button);QVERIFY(button->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,{},button->mapToScene({button->width()/2,button->height()/2}).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),20000);
        const auto painted=*bridge.canvasSnapshot();QVERIFY2(painted.eventIndex->size()>100,qPrintable(workflow->message()));QCOMPARE(layerOrder(painted).size(),std::size_t(8));
        auto canvas=window->findChild<QQuickItem*>("nativeCanvas");QVERIFY(canvas);QTRY_VERIFY(canvas->isVisible());QVERIFY(!qmlObject(window,"analysisSource")->property("visible").toBool() || !qmlObject(window,"reviewPanel")->property("visible").toBool());
        QTest::qWait(100);const auto fullFrame=window->grabWindow();QVERIFY(!fullFrame.isNull());
        bridge.setReplayCount(0);QCOMPARE(bridge.replayCount(),0);QTest::qWait(100);const auto blankFrame=window->grabWindow();QVERIFY(blankFrame!=fullFrame);
        QTest::mousePress(window,Qt::LeftButton,{},point(canvas,painted,240,140));QTest::mouseRelease(window,Qt::LeftButton,{},point(canvas,painted,270,140));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(),painted.eventIndex->size());
        const auto artifactRoot=qEnvironmentVariable("DRAWBORAD_TEST_ARTIFACT_DIR");
        const auto artifact=artifactRoot.isEmpty()?QString{}:QDir(artifactRoot).filePath("reconstruction-"+QString::number(QDateTime::currentMSecsSinceEpoch()));
        if(!artifact.isEmpty()){QVERIFY(QDir().mkpath(artifact));QVERIFY(fullFrame.save(QDir(artifact).filePath(QString::fromUtf8("01-真实笔迹粗绘-保真未验收.png"))));QVERIFY(blankFrame.save(QDir(artifact).filePath(QString::fromUtf8("02-零笔迹回放-空白.png"))));}
        bridge.setReplayCount(bridge.eventCount());QCOMPARE(bridge.replayCount(),bridge.eventCount());
        QVERIFY(workflow->selectCandidate(1));QVERIFY(qmlObject(window,"workspaceTabs")->setProperty("currentIndex",0));QVERIFY(qmlObject(window,"maskTool")->setProperty("currentIndex",3));
        auto editor=window->findChild<QQuickItem*>("maskEditor");QVERIFY(editor);QTest::qWait(100);
        const auto preview=QSizeF(600,400).scaled(QSizeF(editor->width(),editor->height()),Qt::KeepAspectRatio);
        const auto scene=[&](QPointF p){return editor->mapToScene({(editor->width()-preview.width())/2+p.x()*preview.width()/600,(editor->height()-preview.height())/2+p.y()*preview.height()/400}).toPoint();};
        QTest::mousePress(window,Qt::LeftButton,{},scene({230,120}));QTest::mouseMove(window,scene({310,160}));QTest::mouseRelease(window,Qt::LeftButton,{},scene({310,160}));
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),20000);
        QVERIFY(bridge.canvasSnapshot()->eventIndex->size()>painted.eventIndex->size());
        bridge.saveUrl(QUrl::fromLocalFile(project));QVERIFY(!bridge.dirty());const auto saved=*bridge.canvasSnapshot();
        const auto rendered=renderPaintedDocument(saved,{saved.revision});QVERIFY(rendered);
        QVERIFY(QFile::remove(image));bridge.openUrl(QUrl::fromLocalFile(project));QVERIFY(workflow->sourceUrl().isEmpty());
        const auto reopened=*bridge.canvasSnapshot();QVERIFY(reopened.sameContent(saved));
        const auto replay=renderPaintedDocument(reopened,{reopened.revision});QVERIFY(replay);QCOMPARE(replay.value().merged.bytes,rendered.value().merged.bytes);
        const auto psd=owned.filePath("real-layers.psd");QVERIFY2(workflow->exportPsd(QUrl::fromLocalFile(psd)),qPrintable(workflow->message()));
        const auto decoded=readPhotoshop(std::filesystem::path(psd.toStdWString()),{});QVERIFY(decoded);QCOMPARE(decoded.value().photoshop.layers.size(),std::size_t(13));
        const auto stable=*bridge.canvasSnapshot();QVERIFY(!workflow->startReconstruction());QVERIFY(bridge.canvasSnapshot()->sameContent(stable));
        QImage wrong(600,400,QImage::Format_RGB32);wrong.fill(Qt::red);const auto wrongPath=owned.filePath("wrong.png");QVERIFY(wrong.save(wrongPath));
        QVERIFY(!workflow->relinkSource(QUrl::fromLocalFile(wrongPath)));QVERIFY(bridge.canvasSnapshot()->sameContent(stable));
        QVERIFY(workflow->relinkSource(QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"))));QVERIFY(!workflow->sourceUrl().isEmpty());
        QCOMPARE(*bridge.canvasSnapshot()->eventIndex,*stable.eventIndex);
        QVERIFY(bridge.undoDrawing());QCOMPARE(*bridge.canvasSnapshot()->eventIndex,*stable.eventIndex);QVERIFY(bridge.redoDrawing());
        if(!artifact.isEmpty()){bridge.saveUrl(QUrl::fromLocalFile(QDir(artifact).filePath("coffee-regional-strokes.draw")));QVERIFY(QFile::copy(psd,QDir(artifact).filePath("coffee-regional-layers.psd")));}
        const auto beforeCancel=*bridge.canvasSnapshot();QVERIFY(workflow->startReconstruction());workflow->cancel();QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),8000);QVERIFY(bridge.canvasSnapshot()->sameContent(beforeCancel));
        QVERIFY(workflow->startReconstruction());QVERIFY(bridge.addLayer());const auto concurrent=*bridge.canvasSnapshot();
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),30000);QVERIFY(bridge.canvasSnapshot()->sameContent(concurrent));
        QVERIFY(window->setProperty("approvedClose",true));QVERIFY(window->close());
    }
    void maskUndoBudgetIncludesUnassignedPlane(){
        QTest::failOnWarning(QRegularExpression(".*"));
        QTemporaryDir owned;QVERIFY(owned.isValid());
        NewDocumentRequest request;request.dimensions={2048,2048,Unit::Px,300};
        const auto spec=validateDocument(request,{});QVERIFY(spec);
        auto document=Document::create(spec.value(),Id::fromNumber(9));QVERIFY(document);
        const auto source=jsonResource({{"type",SourceReferenceType},{"image",owned.filePath("missing-reference.png")},
            {"sourceHash",QString::fromStdString(sha256(Bytes{}).hex())},{"width",2048},{"height",2048}});
        const Bytes bytes(2048*2048,255);
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{source.id}};
        LayerNode object;object.id=Id::fromNumber(2);object.parent=Document::rootId();object.kind=NodeKind::Object;object.name="Mask budget fixture";object.maskId=mask.id;
        LayerNode layer;layer.id=Id::fromNumber(3);layer.parent=object.id;layer.objectId=object.id;layer.name="Paint";
        Command command;command.operations={InstallResource{source},InstallResource{mask},AddNode{object},AddNode{layer}};
        QVERIFY(document.value().apply(command,0));
        const auto path=owned.filePath("mask-budget.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(path.toStdWString()),document.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(path));
        auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);QVERIFY(workflow->hasReview());
        for(int i=0;i<16;++i)QVERIFY(workflow->editRectangle(false,i,0,i+1,1));
        QVERIFY(!workflow->editRectangle(false,16,0,17,1)); // 17 * (mask4MiB + unassigned4MiB) >128MiB.
        QVERIFY(workflow->undo());QVERIFY(workflow->editRectangle(false,16,0,17,1));
        QVERIFY(workflow->accept());QVERIFY(workflow->editRectangle(false,17,0,18,1));
        // Shared mask resources can be referenced by many objects. Loading the
        // review must bound copies, even when the project file itself is small.
        auto duplicate=Document::create(spec.value(),Id::fromNumber(10));QVERIFY(duplicate);
        Command many;many.operations={InstallResource{source},InstallResource{mask}};
        for(unsigned i=0;i<14;++i){auto copy=object;copy.id=Id::fromNumber(100+i);many.operations.push_back(AddNode{copy});}
        QVERIFY(duplicate.value().apply(many,0));
        const auto large=owned.filePath("shared-mask-budget.draw");QVERIFY(ProjectStore{}.save(std::filesystem::path(large.toStdWString()),duplicate.value().snapshot()));
        bridge.openUrl(QUrl::fromLocalFile(large));QVERIFY(bridge.hasDocument());QVERIFY(!workflow->hasReview());
    }
    void cancelAndFailurePreserveCurrentPainting(){
        QTest::failOnWarning(QRegularExpression(".*"));
        QtBridge bridge;bridge.createDocument(600,400,300);
        auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);
        const auto model=QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE"));
        const auto image=QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"));
        QVERIFY2(selectReadyModel(workflow,model),qPrintable(workflow->modelStatus()));const auto stable=*bridge.canvasSnapshot();
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window && QTest::qWaitForWindowExposed(window));
        QVERIFY2(workflow->startImage(image),qPrintable(workflow->message()));
        auto cancel=window->findChild<QQuickItem*>("cancelWorkerButton");QVERIFY(cancel);QTRY_VERIFY(cancel->isVisible() && cancel->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,{},cancel->mapToScene({cancel->width()/2,cancel->height()/2}).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),8000);
        QVERIFY(bridge.canvasSnapshot()->sameContent(stable));
        QTemporaryDir owned;QVERIFY(owned.isValid());
        workflow->setModelPackage(QUrl::fromLocalFile(owned.filePath("missing-model")));
        QVERIFY(workflow->checkModel());QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),10000);QCOMPARE(workflow->modelState(),QStringLiteral("MissingModel"));QVERIFY(!workflow->startImage(image));
        QVERIFY(bridge.canvasSnapshot()->sameContent(stable));
        QImage transparent(8,8,QImage::Format_ARGB32);transparent.fill(QColor(255,0,0,128));
        const auto partial=owned.filePath("partial-alpha.png");QVERIFY(transparent.save(partial));
        QVERIFY2(selectReadyModel(workflow,model),qPrintable(workflow->modelStatus()));QVERIFY(workflow->startImage(QUrl::fromLocalFile(partial)));
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),20000);QVERIFY(bridge.canvasSnapshot()->sameContent(stable));
        QVERIFY(workflow->message().contains("review.partial-alpha-not-supported"));
        window->setProperty("approvedClose",true);window->close();
    }
    void realCoffeeReviewSaveReopen(){
        QTest::failOnWarning(QRegularExpression(".*"));
        QTemporaryDir owned;QVERIFY(owned.isValid());
        const auto artifactRoot=qEnvironmentVariable("DRAWBORAD_TEST_ARTIFACT_DIR");
        const auto artifact=artifactRoot.isEmpty()?QString{}:QDir(artifactRoot).filePath(QString::number(QDateTime::currentMSecsSinceEpoch()));
        if(!artifact.isEmpty())QVERIFY(QDir().mkpath(artifact));
        const auto image=owned.filePath("coffee-cc0.png");
        QVERIFY(QFile::copy(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"),image));
        QtBridge bridge;bridge.createDocument(600,400,300);
        auto workflow=qobject_cast<WorkflowController*>(bridge.workflow());QVERIFY(workflow);
        QVERIFY2(selectReadyModel(workflow,QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_PACKAGE"))),qPrintable(workflow->modelStatus()));
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto before=*bridge.canvasSnapshot();
        unsigned ticks=0;QTimer heartbeat;heartbeat.setInterval(20);connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start();
        QVERIFY2(workflow->startImage(QUrl::fromLocalFile(image)),qPrintable(workflow->message()));
        QVERIFY(workflow->busy());
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),600000);
        QVERIFY2(workflow->hasReview(),qPrintable(workflow->message()));
        QVERIFY(ticks>100);QVERIFY(bridge.canvasSnapshot()->sameContent(before));
        QCOMPARE(workflow->candidates().size(),8);QVERIFY(workflow->pending());
        const auto blocked=owned.filePath("unaccepted-must-not-save.draw");
        bridge.saveUrl(QUrl::fromLocalFile(blocked));QVERIFY(!QFile::exists(blocked));QVERIFY(workflow->pending());
        QVERIFY(workflow->includeCandidate(2,true));QVERIFY(workflow->includeCandidate(4,true));
        QVERIFY(workflow->selectCandidate(2));const QImage spoonBowl(workflow->overlayUrl().toLocalFile());
        QVERIFY(workflow->selectCandidate(4));const QImage spoonShaft(workflow->overlayUrl().toLocalFile());
        QVERIFY(workflow->selectCandidate(2));QVERIFY(workflow->mergeIncluded());QCOMPARE(workflow->candidates().size(),7);
        const QImage combined(workflow->overlayUrl().toLocalFile());
        for(int y=0;y<400;++y)for(int x=0;x<600;++x)QCOMPARE(combined.pixelColor(x,y).alpha(),std::max(spoonBowl.pixelColor(x,y).alpha(),spoonShaft.pixelColor(x,y).alpha()));
        QVERIFY(workflow->undo());QCOMPARE(workflow->candidates().size(),8);
        QVERIFY(workflow->includeCandidate(2,false));QVERIFY(workflow->includeCandidate(4,false));
        QVERIFY(workflow->selectCandidate(-1));QCOMPARE(QImage(workflow->overlayUrl().toLocalFile()).pixelColor(0,0).alpha(),110);
        QVERIFY(workflow->selectCandidate(5));
        QImage maskBefore(workflow->overlayUrl().toLocalFile());QVERIFY(!maskBefore.isNull());
        QPoint interior;
        for(int y=5;y<maskBefore.height()-5 && interior.isNull();++y)for(int x=5;x<maskBefore.width()-5;++x){
            if(maskBefore.pixelColor(x,y).alpha()>0 && maskBefore.pixelColor(x+4,y+4).alpha()>0){interior={x,y};break;}
        }
        QVERIFY(!interior.isNull());
        auto tool=qmlObject(window,"maskTool");QVERIFY(tool);QVERIFY(tool->setProperty("currentIndex",2));
        auto editor=window->findChild<QQuickItem*>("maskEditor");QVERIFY(editor);
        QTest::qWait(100);
        const auto preview=QSizeF(600,400).scaled(QSizeF(editor->width(),editor->height()),Qt::KeepAspectRatio);
        const auto scene=[&](QPointF p){return editor->mapToScene({(editor->width()-preview.width())/2+p.x()*preview.width()/600,
            (editor->height()-preview.height())/2+p.y()*preview.height()/400}).toPoint();};
        QTest::mousePress(window,Qt::LeftButton,{},scene(interior));
        QTest::mouseMove(window,scene(interior+QPoint(4,4)));
        QTest::mouseRelease(window,Qt::LeftButton,{},scene(interior+QPoint(4,4)));
        QImage erased(workflow->overlayUrl().toLocalFile());QVERIFY(erased!=maskBefore);
        QVERIFY(workflow->undo());QCOMPARE(QImage(workflow->overlayUrl().toLocalFile()),maskBefore);
        QVERIFY(workflow->editRectangle(false,interior.x(),interior.y(),interior.x()+4,interior.y()+4));
        QVERIFY(workflow->includeCandidate(3,true));QVERIFY(workflow->renameCandidate(3,QString::fromUtf8("杯内可见区域")));
        QVERIFY(workflow->includeCandidate(5,true));QVERIFY(workflow->renameCandidate(5,QString::fromUtf8("咖啡液面（已修正）")));
        QTRY_COMPARE(qmlObject(window,"maskOverlay")->property("status").toInt(),1);
        QTest::qWait(80);
        if(!artifact.isEmpty())QVERIFY(window->grabWindow().save(QDir(artifact).filePath(QString::fromUtf8("01-真实咖啡图与审阅蒙版.png"))));
        // Explicit user acceptance is the only operation that replaces artwork.
        QVERIFY(workflow->accept());QVERIFY(!workflow->pending());QVERIFY(workflow->editingExisting());
        auto accepted=*bridge.canvasSnapshot();QCOMPARE(layerOrder(accepted).size(),std::size_t(2));QVERIFY(accepted.eventIndex->empty());
        std::vector<Hash> maskIds;
        for(const auto& [id,node]:*accepted.nodes){
            Q_UNUSED(id);QVERIFY(node.sourceResource.empty());QVERIFY(node.content.empty());QCOMPARE(node.sourceKind,SourceKind::Reconstructed);
            if(!node.maskId.empty()){maskIds.push_back(node.maskId);QCOMPARE(accepted.resources->at(node.maskId).kind,ResourceKind::Mask);}
        }
        QCOMPARE(maskIds.size(),std::size_t(2));QVERIFY(maskIds[0]!=maskIds[1]);
        for(const auto& [hash,resource]:*accepted.resources){Q_UNUSED(hash);QVERIFY(resource.kind==ResourceKind::Mask || resource.kind==ResourceKind::Reference);}
        const auto project=owned.filePath("coffee-reviewed.draw");bridge.saveUrl(QUrl::fromLocalFile(project));QVERIFY(!bridge.dirty());
        bridge.openUrl(QUrl::fromLocalFile(project));QCOMPARE(layerOrder(*bridge.canvasSnapshot()).size(),std::size_t(2));
        QVERIFY(workflow->hasReview());QCOMPARE(workflow->candidates().size(),2);QVERIFY(!workflow->pending());
        QCOMPARE(bridge.canvasSnapshot()->resources->size(),accepted.resources->size());
        for(const auto hash:maskIds)QCOMPARE(*bridge.canvasSnapshot()->resources->at(hash).bytes,*accepted.resources->at(hash).bytes);

        // Save/reopen retains editable masks. Applying one after new paint must
        // preserve that paint, and the canvas clips at the object mask.
        QVERIFY(qmlObject(window,"workspaceTabs")->setProperty("currentIndex",1));
        auto canvas=window->findChild<QQuickItem*>("nativeCanvas");QVERIFY(canvas);QTRY_VERIFY(canvas->isVisible());
        const auto snapshot=*bridge.canvasSnapshot();const auto layer=*bridge.activeLayer();
        const auto object=snapshot.nodes->at(layer).objectId;
        const auto& mask=*snapshot.resources->at(snapshot.nodes->at(object).maskId).bytes;
        QPoint inside;
        for(int y=10;y<390 && inside.isNull();++y)for(int x=10;x<590;++x)if(mask[std::size_t(y*600+x)]){inside={x,y};break;}
        QVERIFY(!inside.isNull());
        QTest::mousePress(window,Qt::LeftButton,{},point(canvas,snapshot,1,inside.y()));
        QTest::mouseMove(window,point(canvas,snapshot,300,inside.y()));
        QTest::mouseRelease(window,Qt::LeftButton,{},point(canvas,snapshot,598,inside.y()));
        QTRY_COMPARE(bridge.canvasSnapshot()->eventIndex->size(),std::size_t(1));
        const auto painted=*bridge.canvasSnapshot();QVERIFY(rendered(painted,layer,inside.x(),inside.y()).alpha()>0);
        QCOMPARE(rendered(painted,layer,1,inside.y()).alpha(),0);
        QVERIFY(workflow->selectCandidate(0));QVERIFY(workflow->editRectangle(true,0,0,2,2));QVERIFY(workflow->accept());
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(),std::size_t(1));
        if(!artifact.isEmpty())QVERIFY(window->grabWindow().save(QDir(artifact).filePath(QString::fromUtf8("02-仅真实笔迹-尚未重建.png"))));
        bridge.saveUrl(QUrl::fromLocalFile(project));QVERIFY(!bridge.dirty());
        const auto saved=*bridge.canvasSnapshot();
        QVERIFY(QFile::remove(image)); // This test's own input copy only.
        bridge.openUrl(QUrl::fromLocalFile(project));QVERIFY(workflow->hasReview());QVERIFY(workflow->sourceUrl().isEmpty());
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(),std::size_t(1));
        QCOMPARE(rendered(*bridge.canvasSnapshot(),layer,inside.x(),inside.y()),rendered(saved,layer,inside.x(),inside.y()));
        if(!artifact.isEmpty()){
            QVERIFY(QFile::copy(project,QDir(artifact).filePath("coffee-reviewed-source-free.draw")));
        }

        // The same worker cancellation/failure path preserves a real project.
        const auto stable=*bridge.canvasSnapshot();
        QVERIFY(workflow->startImage(QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"))));
        QTest::qWait(100);workflow->cancel();QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),8000);
        QVERIFY(bridge.canvasSnapshot()->sameContent(stable));QVERIFY(workflow->hasReview());
        workflow->setModelPackage(QUrl::fromLocalFile(owned.filePath("missing-model")));
        QVERIFY(workflow->checkModel());QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(),10000);QCOMPARE(workflow->modelState(),QStringLiteral("MissingModel"));
        QVERIFY(!workflow->startImage(QUrl::fromLocalFile(qEnvironmentVariable("DRAWBORAD_COFFEE_IMAGE"))));QVERIFY(bridge.canvasSnapshot()->sameContent(stable));QVERIFY(workflow->hasReview());
        QVERIFY(window->close());
    }
};
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasItem>("Drawborad.Native",1,0,"NativeCanvas");
    RealImageReviewTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "RealImageReviewTests.moc"
