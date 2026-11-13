#include "apps/studio/QtBridge.h"
#include "platform/CanvasItem.h"
#include "core/LineartRaster.h"
#include "storage/ProjectStore.h"
#include "view/LayerTree.h"
#include "ui/WorkflowController.h"
#include "ui/ReviewJob.h"
#include <QBuffer>
#include <QDir>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
using namespace drawborad;
namespace {
QString selected(const QtBridge& bridge) { return bridge.property("activeLayerId").toString(); }
QVariantList rows(const QtBridge& bridge) { return bridge.property("layers").toList(); }
NodeId nodeId(const QString& text) {
    const auto bytes = QByteArray::fromHex(text.toLatin1());
    NodeId id;
    if (bytes.size() == 16)
        std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()), 16, id.bytes.begin());
    return id;
}
bool addLayer(QtBridge& bridge) {
    bool result = false;
    return QMetaObject::invokeMethod(&bridge, "addLayer", Q_RETURN_ARG(bool, result)) && result;
}
bool selectLayer(QtBridge& bridge, const QString& id) {
    bool result = false;
    return QMetaObject::invokeMethod(&bridge, "selectLayer", Q_RETURN_ARG(bool, result),
                                     Q_ARG(QString, id)) && result;
}
bool setVisible(QtBridge& bridge, const QString& id, bool visible) {
    bool result = false;
    return QMetaObject::invokeMethod(&bridge, "setLayerVisible", Q_RETURN_ARG(bool, result),
                                     Q_ARG(QString, id), Q_ARG(bool, visible)) && result;
}
StrokeEvent stroke(const DocumentSnapshot& snapshot, NodeId layer, std::uint64_t number,
                   double y, std::array<double, 4> color = {0.08, 0.08, 0.08, 0.0}) {
    StrokeEvent event;
    event.id = Id::fromNumber(number);
    event.layerId = layer;
    event.objectId = snapshot.nodes->at(layer).objectId;
    event.parentRevision = snapshot.revision;
    event.toolId = "pencil";
    event.color.activeChannels = 3;
    event.color.channels = color;
    event.color.alpha = 1;
    event.color.profileId = snapshot.spec.colorSpec.profileId;
    event.startUs = 1;
    event.endUs = 2;
    event.geometry = {{30, y, 1, {}, {}, 1, 0, 0}, {90, y, 1, {}, {}, 2, 0, 0}};
    event.engineVersion = 2;
    event.style.width = 8;
    return event;
}
QPoint canvasPoint(QQuickItem* canvas, QPointF document, const DocumentSnapshot& snapshot) {
    const auto scale = std::min(canvas->width() / double(snapshot.spec.pixelSize.width),
                                canvas->height() / double(snapshot.spec.pixelSize.height));
    const auto ox = (canvas->width() - double(snapshot.spec.pixelSize.width) * scale) * .5;
    const auto oy = (canvas->height() - double(snapshot.spec.pixelSize.height) * scale) * .5;
    return canvas->mapToScene({ox + document.x() * scale, oy + document.y() * scale}).toPoint();
}
void draw(QQuickWindow* window, QQuickItem* canvas, const DocumentSnapshot& snapshot, double y) {
    QTest::mousePress(window, Qt::LeftButton, {}, canvasPoint(canvas, {30, y}, snapshot));
    QTest::mouseMove(window, canvasPoint(canvas, {60, y}, snapshot));
    QTest::mouseRelease(window, Qt::LeftButton, {}, canvasPoint(canvas, {90, y}, snapshot));
}
QColor pixel(QQuickWindow* window, QQuickItem* canvas, const DocumentSnapshot& snapshot,
             QPointF document) {
    const auto frame = window->grabWindow();
    if (frame.isNull()) return QColor(Qt::white);
    const auto position = canvasPoint(canvas, document, snapshot);
    return frame.pixelColor(qRound(position.x() * double(frame.width()) / window->width()),
                            qRound(position.y() * double(frame.height()) / window->height()));
}
QColor referencePixel(const DocumentSnapshot& snapshot, QPointF point) {
    std::array<double, 3> rgb{250, 249, 246};
    if(!snapshot.spec.background.transparent)for(std::size_t c=0;c<3;c++)rgb[c]=snapshot.spec.background.paperColor.channels[c]*255;
    ReferenceRenderer renderer;
    for (const auto id : layerOrder(snapshot)) {
        if (!layerVisible(snapshot, id) || !nativeLayerSupported(snapshot, id)) continue;
        const auto tile = renderer.renderRegion(snapshot, id,
            {std::int64_t(std::floor(point.x())), std::int64_t(std::floor(point.y())), 1, 1},
            {snapshot.revision, snapshot.eventIndex->size(), UINT64_MAX});
        if (!tile) return QColor{};
        const auto& bytes = tile.value().bytes;
        const double alpha = bytes.at(3) / 255.0 * snapshot.nodes->at(id).opacity;
        for (std::size_t channel = 0; channel < 3; ++channel)
            rgb[channel] = bytes.at(channel) * alpha + rgb[channel] * (1 - alpha);
    }
    return {qRound(rgb[0]), qRound(rgb[1]), qRound(rgb[2])};
}
bool matchesReference(QQuickWindow* window, QQuickItem* canvas, const DocumentSnapshot& snapshot,
                      QPointF point) {
    const auto expected = referencePixel(snapshot, point);
    const auto actual = pixel(window, canvas, snapshot, point);
    return expected.isValid() && std::abs(actual.red() - expected.red()) <= 4 &&
        std::abs(actual.green() - expected.green()) <= 4 && std::abs(actual.blue() - expected.blue()) <= 4;
}
bool clickItem(QQuickWindow* window, QObject* root, const QString& name) {
    Q_UNUSED(root);
    // ListView delegates belong to the visual tree, not necessarily the window's QObject tree.
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == name && item->isVisible()) return item;
        for (auto child : item->childItems()) if (auto match = find(child)) return match;
        return nullptr;
    };
    auto item = find(window->contentItem());
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    if (auto sidebar = window->findChild<QQuickItem*>("sidebar")) {
        auto ancestor = item->parentItem();
        while (ancestor && ancestor != sidebar) ancestor = ancestor->parentItem();
        if (ancestor) {
            const auto y = item->mapToItem(sidebar, {0, item->height() / 2}).y();
            if (y < 0 || y > sidebar->height()) {
                auto flick = sidebar->property("contentItem").value<QObject*>();
                if (!flick || !flick->setProperty("contentY", flick->property("contentY").toDouble() + y - sidebar->height() / 2)) return false;
                QCoreApplication::processEvents();
            }
        }
    }
    QTest::mouseClick(window, Qt::LeftButton, {},
                      item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    return true;
}
QObject* qmlObject(QObject* window, const QString& id) {
    QQmlExpression expression(qmlContext(window), window, id);
    const auto value = expression.evaluate();
    return expression.hasError() ? nullptr : value.value<QObject*>();
}
QQuickItem* dialogButton(QObject* dialog, const QString& text) {
    auto content = dialog->property("contentItem").value<QQuickItem*>();
    if (!content) return nullptr;
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* item) -> QQuickItem* {
        if (item->property("text").toString() == text && item->isVisible() && item->isEnabled())
            return item;
        for (auto child : item->childItems()) if (auto match = find(child)) return match;
        return nullptr;
    };
    return find(content);
}
bool clickDialogButton(QQuickWindow* window, QObject* dialog, const QString& text) {
    auto button = dialogButton(dialog, text);
    if (!button || button->width() <= 0 || button->height() <= 0) return false;
    QTest::mouseClick(window, Qt::LeftButton, {},
        button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
    return true;
}
}
class LayerUiTests : public QObject {
    Q_OBJECT
  private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*")); }
    void importedRasterDisplaysWithoutFlatteningDocument() {
        NewDocumentRequest request;request.dimensions={128,128,Unit::Px,300};
        auto spec=validateDocument(request,{});QVERIFY(spec);auto doc=Document::create(spec.value(),Id::fromNumber(91));QVERIFY(doc);
        Bytes rgba(128*128*4,0);for(unsigned y=20;y<100;y++)for(unsigned x=20;x<100;x++){const auto at=(y*128+x)*4;rgba[at]=255;rgba[at+3]=255;}
        auto imported=makeLineartRasterCommand(rgba,{137,80,78,71,13,10,26,10},128,128,Id::fromNumber(92));QVERIFY(imported);QVERIFY(doc.value().apply(imported.value(),0));
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto file=directory.filePath("imported.draw");ProjectStore store;
        QVERIFY(store.save(std::filesystem::path(file.toStdWString()),doc.value().snapshot()));
        QtBridge bridge;bridge.openUrl(QUrl::fromLocalFile(file));QVERIFY(bridge.canvasSnapshot());
        const auto original=*bridge.canvasSnapshot();QQuickWindow window;window.resize(320,320);CanvasItem canvas(window.contentItem());canvas.setSize({320,320});canvas.setProperty("bridge",QVariant::fromValue<QObject*>(&bridge));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(pixel(&window,&canvas,original,{60,60}).red()>250);
        QTRY_VERIFY(pixel(&window,&canvas,original,{60,60}).green()<5);
        QVERIFY(canvas.renderError().isEmpty());QVERIFY(bridge.canvasSnapshot()->sameContent(original));
    }
    void missingModelPreflightUsesWorkerWithoutChangingDocument() {
        QtBridge bridge;
        bridge.createDocument(64, 64, 300);
        const auto before = *bridge.canvasSnapshot();
        auto workflow = qobject_cast<WorkflowController*>(bridge.workflow());
        QVERIFY(workflow);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        workflow->setModelPackage(QUrl::fromLocalFile(directory.filePath("missing-model")));
        QVERIFY(!workflow->modelReady());
        QVERIFY(workflow->checkModel());
        QVERIFY(workflow->busy());
        QTRY_VERIFY_WITH_TIMEOUT(!workflow->busy(), 10000);
        QCOMPARE(workflow->modelState(), QStringLiteral("MissingModel"));
        QVERIFY(!workflow->modelReady());
        QVERIFY(!workflow->startImage(QUrl::fromLocalFile(directory.filePath("missing.png"))));
        QVERIFY(bridge.canvasSnapshot()->sameContent(before));
    }
    void zoomAndPanKeepMouseInputInDocumentCoordinates() {
        QtBridge bridge;
        bridge.createDocument(128, 128, 300);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto canvas = window->findChild<CanvasItem*>("nativeCanvas");
        QVERIFY(canvas);
        const auto before = *bridge.canvasSnapshot();
        const auto anchor = QPointF(canvas->width() / 2, canvas->height() / 2);
        QVERIFY(clickItem(window, window, "zoomInButton"));
        QCOMPARE(canvas->zoom(), 1.25);
        QVERIFY(canvas->zoomAt(1.6, anchor.x(), anchor.y()));
        QCOMPARE(canvas->zoom(), 2.0);
        QVERIFY(canvas->panBy(24, -18));
        const auto scale = std::min(canvas->width() / 128, canvas->height() / 128) * 2;
        const auto point = [&](double x, double y) {
            return canvas->mapToScene({anchor.x() + 24 + (x - 64) * scale,
                                       anchor.y() - 18 + (y - 64) * scale}).toPoint();
        };
        QTest::mousePress(window, Qt::LeftButton, {}, point(55, 60));
        QTest::mouseMove(window, point(64, 60));
        QTest::mouseRelease(window, Qt::LeftButton, {}, point(73, 60));
        QTRY_COMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(1));
        const auto& stroke = bridge.canvasSnapshot()->eventIndex->front();
        QVERIFY(std::abs(stroke.geometry.front().x - 55) < 1);
        QVERIFY(std::abs(stroke.geometry.front().y - 60) < 1);
        QVERIFY(std::abs(stroke.geometry.back().x - 73) < 1);
        QVERIFY(std::abs(stroke.geometry.back().y - 60) < 1);
        QVERIFY(!canvas->zoomAt(std::numeric_limits<double>::infinity(), 0, 0));
        QVERIFY(!canvas->panBy(std::numeric_limits<double>::quiet_NaN(), 0));
        QVERIFY(canvas->zoomAt(1000, anchor.x(), anchor.y()));
        QVERIFY(canvas->zoom() <= 16);
        canvas->fitDocument();
        QCOMPARE(canvas->zoom(), 1.0);
        QVERIFY(bridge.canvasSnapshot()->documentId == before.documentId);
        bridge.createDocument(64, 96, 300);
        QCOMPARE(canvas->zoom(), 1.0);
        window->setProperty("approvedClose", true);
        window->close();
    }
    void exifPreviewAndMaskEditsUseTheSameOrientedCoordinates() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage original(96, 64, QImage::Format_RGB32);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 96; ++x)
                original.setPixelColor(x, y, y < 32 ? (x < 48 ? Qt::red : Qt::green)
                                                   : (x < 48 ? Qt::blue : Qt::yellow));
        QByteArray encoded;
        QBuffer buffer(&encoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(original.save(&buffer, "JPEG", 100));
        const auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
        QByteArray marker = QByteArray::fromHex("ffe10000");
        marker[2] = char((exif.size() + 2) >> 8);
        marker[3] = char((exif.size() + 2) & 255);
        marker += exif;
        encoded.insert(2, marker);
        const auto imagePath = directory.filePath("orientation-6.jpg");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write(encoded), qint64(encoded.size()));
        image.close();
        NewDocumentRequest request;
        request.dimensions = {64, 96, Unit::Px, 300};
        const auto spec = validateDocument(request, {});
        QVERIFY(spec);
        auto document = Document::create(spec.value(), Id::fromNumber(6800));
        QVERIFY(document);
        const auto source = jsonResource({{"type", SourceReferenceType}, {"image", imagePath},
            {"sourceHash", QString::fromStdString(sha256(std::span(reinterpret_cast<const std::uint8_t*>(encoded.constData()), std::size_t(encoded.size()))).hex())},
            {"width", 64}, {"height", 96}});
        Bytes pixels(64 * 96, 0);
        for (unsigned y = 0; y < 48; ++y)
            for (unsigned x = 0; x < 32; ++x) pixels[y * 64 + x] = 255;
        Resource mask{sha256(pixels), ResourceKind::Mask, std::make_shared<const Bytes>(pixels), {source.id}};
        LayerNode object;
        object.id = Id::fromNumber(6801); object.parent = Document::rootId();
        object.kind = NodeKind::Object; object.name = "Oriented blue corner"; object.maskId = mask.id;
        LayerNode layer;
        layer.id = Id::fromNumber(6802); layer.parent = object.id; layer.objectId = object.id; layer.name = "Paint";
        Command setup;
        setup.operations = {InstallResource{source}, InstallResource{mask}, AddNode{object}, AddNode{layer}};
        QVERIFY(document.value().apply(setup, 0));
        const auto path = QUrl::fromLocalFile(directory.filePath("oriented.draw"));
        QVERIFY(ProjectStore{}.save(std::filesystem::path(path.toLocalFile().toStdWString()), document.value().snapshot()));
        QtBridge bridge;
        bridge.openUrl(path);
        auto workflow = qobject_cast<WorkflowController*>(bridge.workflow());
        QVERIFY(workflow && workflow->hasReview());
        QCOMPARE(workflow->imageWidth(), 64);
        QCOMPARE(workflow->imageHeight(), 96);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto sourceItem = qmlObject(window, "analysisSource");
        auto overlay = qmlObject(window, "maskOverlay");
        QVERIFY(sourceItem && overlay);
        QTRY_COMPARE(sourceItem->property("status").toInt(), 1);
        QTRY_COMPARE(overlay->property("status").toInt(), 1);
        QVERIFY(std::abs(sourceItem->property("paintedWidth").toDouble() /
                         sourceItem->property("paintedHeight").toDouble() - 2.0 / 3.0) < .01);
        QCOMPARE(sourceItem->property("paintedWidth"), overlay->property("paintedWidth"));
        QCOMPARE(sourceItem->property("paintedHeight"), overlay->property("paintedHeight"));
        QVERIFY(qmlObject(window, "maskTool")->setProperty("currentIndex", 2));
        auto editor = window->findChild<QQuickItem*>("maskEditor");
        QVERIFY(editor);
        const auto snapshot = *bridge.canvasSnapshot();
        QTRY_VERIFY(pixel(window, editor, snapshot, {12, 15}).blue() >
                    pixel(window, editor, snapshot, {12, 15}).red() + 100);
        QTRY_VERIFY(pixel(window, editor, snapshot, {48, 15}).red() >
                    pixel(window, editor, snapshot, {48, 15}).blue() + 100);
        QTest::mousePress(window, Qt::LeftButton, {}, canvasPoint(editor, {8, 10}, snapshot));
        QTest::mouseRelease(window, Qt::LeftButton, {}, canvasPoint(editor, {16, 20}, snapshot));
        QVERIFY(workflow->accept());
        const auto accepted = *bridge.canvasSnapshot();
        const auto& edited = *accepted.resources->at(accepted.nodes->at(object.id).maskId).bytes;
        QCOMPARE(edited[15 * 64 + 12], std::uint8_t(0));
        QCOMPARE(edited[30 * 64 + 20], std::uint8_t(255));
        bridge.saveUrl(path);
        QVERIFY(!bridge.dirty());
        bridge.openUrl(path);
        QVERIFY(bridge.canvasSnapshot()->sameContent(accepted));
        QVERIFY(window->close());
    }
    void closeAndSaveFlow_data() {
        QTest::addColumn<QString>("action");
        for (const auto* action : {"save", "cancel-save", "failed-save", "manual-save",
                                   "cancel-close", "discard", "clean-close"})
            QTest::newRow(action) << QString::fromLatin1(action);
    }
    void closeAndSaveFlow() {
        QFETCH(QString, action);
        QtBridge bridge;
        bridge.createDocument(128, 128, 300);
        QVERIFY(bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(selected(bridge)), 500, 60)));
        const auto expected = *bridge.canvasSnapshot();
        QVERIFY(bridge.dirty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = QUrl::fromLocalFile(directory.filePath("close-flow.draw"));
        const auto failedFile = QUrl::fromLocalFile(directory.filePath("cannot-save.draw"));
        if (action == "failed-save") {
            QFile target(failedFile.toLocalFile());
            QVERIFY(target.open(QIODevice::WriteOnly));
            QCOMPARE(target.write("preserve this invalid project"), qint64(29));
        }
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto closeDialog = qmlObject(window, "closeDialog");
        auto saveDialog = qmlObject(window, "saveDialog");
        QVERIFY(closeDialog && saveDialog);
        QVERIFY(saveDialog->setProperty("currentFolder", QUrl::fromLocalFile(directory.path())));
        if (action == "clean-close") {
            bridge.saveUrl(file);
            QVERIFY(!bridge.dirty());
            QVERIFY(window->close());
            QTRY_VERIFY(!window->isVisible());
            QVERIFY(!closeDialog->property("visible").toBool());
            return;
        }
        if (action == "manual-save") {
            QVERIFY(QMetaObject::invokeMethod(saveDialog, "open"));
        } else {
            QVERIFY(!window->close());
            QTRY_VERIFY(closeDialog->property("visible").toBool());
            QVERIFY(window->isVisible());
            const auto choice = action == "cancel-close" ? "Cancel" : action == "discard" ? "Discard" : "Save";
            QTRY_VERIFY(dialogButton(closeDialog, QString::fromLatin1(choice)));
            QVERIFY(clickDialogButton(window, closeDialog, QString::fromLatin1(choice)));
            if (action == "discard") {
                QTRY_VERIFY(!window->isVisible());
                QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
                QVERIFY(!QFile::exists(file.toLocalFile()));
                return;
            }
            if (action == "cancel-close") {
                QTRY_VERIFY(!closeDialog->property("visible").toBool());
                QVERIFY(window->isVisible());
                QVERIFY(bridge.dirty());
                QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
                return;
            }
        }
        QTRY_VERIFY(saveDialog->property("visible").toBool());
        // Exercise the real QML picker callbacks with an OS-selected URL. Native picker
        // navigation is outside this test; serialization and reopening use the real store.
        if (action == "cancel-save") {
            QVERIFY(QMetaObject::invokeMethod(saveDialog, "reject"));
        } else {
            const auto destination = action == "failed-save"
                ? failedFile : file;
            QVERIFY(saveDialog->setProperty("selectedFile", destination));
            QVERIFY(QMetaObject::invokeMethod(saveDialog, "accepted"));
            // Closing the picker before accepted would emit rejected and cancel the request.
            QVERIFY(QMetaObject::invokeMethod(saveDialog, "close"));
        }
        if (action == "save") {
            QVERIFY(!bridge.dirty());
            QVERIFY(QFile::exists(file.toLocalFile()));
            QtBridge reopened;
            reopened.openUrl(file);
            QVERIFY(reopened.canvasSnapshot());
            QVERIFY(reopened.canvasSnapshot()->sameContent(expected));
            QTRY_VERIFY2(!window->isVisible(), "Successful Save chosen while closing must finish closing");
        } else {
            QVERIFY(window->isVisible());
            QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
            if (action == "manual-save") {
                QVERIFY(!bridge.dirty());
                QVERIFY(QFile::exists(file.toLocalFile()));
            } else {
                QVERIFY(bridge.dirty());
                QVERIFY(!QFile::exists(file.toLocalFile()));
                if (action == "failed-save") {
                    QVERIFY(!bridge.message().isEmpty());
                    QFile target(failedFile.toLocalFile());
                    QVERIFY(target.open(QIODevice::ReadOnly));
                    QCOMPARE(target.readAll(), QByteArray("preserve this invalid project"));
                }
                // A later ordinary Save must not inherit a cancelled/failed close request.
                QVERIFY(QMetaObject::invokeMethod(saveDialog, "open"));
                QVERIFY(saveDialog->setProperty("selectedFile", file));
                QVERIFY(QMetaObject::invokeMethod(saveDialog, "accepted"));
                QVERIFY(QMetaObject::invokeMethod(saveDialog, "close"));
                QVERIFY(!bridge.dirty());
                QVERIFY(window->isVisible());
                QtBridge reopened;
                reopened.openUrl(file);
                QVERIFY(reopened.canvasSnapshot());
                QVERIFY(reopened.canvasSnapshot()->sameContent(expected));
            }
        }
    }
    void bridgePreservesLayerIdentityAndSeparatePaint() {
        QtBridge bridge;
        bridge.createDocument(128, 128, 300);
        QVERIFY2(bridge.property("layers").isValid(), "The bridge must expose the document layer list");
        QCOMPARE(rows(bridge).size(), 1);
        const auto first = selected(bridge);
        QCOMPARE(first.size(), 32);
        QVERIFY(bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(first), 100, 30)));
        QVERIFY(addLayer(bridge));
        const auto second = selected(bridge);
        QVERIFY(first != second);
        QCOMPARE(rows(bridge).size(), 2);
        QCOMPARE(rows(bridge).front().toMap().value("id").toString(), second);
        QVERIFY(bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(second), 101, 70)));
        const auto expected = *bridge.canvasSnapshot();
        QCOMPARE(expected.eventIndex->at(0).layerId, nodeId(first));
        QCOMPARE(expected.eventIndex->at(1).layerId, nodeId(second));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = QUrl::fromLocalFile(directory.filePath("two-layers.draw"));
        bridge.saveUrl(file);
        QVERIFY(!bridge.dirty());
        QVERIFY(selectLayer(bridge, first));
        QVERIFY(!bridge.dirty());
        QVERIFY(!selectLayer(bridge, "not-a-layer"));
        QCOMPARE(selected(bridge), first);
        QVERIFY(!bridge.dirty());
        bridge.openUrl(QUrl::fromLocalFile(directory.filePath("missing.draw")));
        QCOMPARE(selected(bridge), first);
        QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
        QVERIFY(setVisible(bridge, second, false));
        QVERIFY(bridge.dirty());
        bridge.openUrl(file);
        QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
        QCOMPARE(rows(bridge).size(), 2);
        QVERIFY(!bridge.dirty());
    }
    void inputKeepsItsOriginalLayerAndCancelsAfterDocumentReplacement() {
        QtBridge bridge;
        bridge.createDocument(128, 128, 300);
        const auto first = selected(bridge);
        QVERIFY(addLayer(bridge));
        const auto second = selected(bridge);
        QVERIFY(selectLayer(bridge, first));
        QQuickWindow window;
        window.resize(320, 320);
        CanvasItem canvas(window.contentItem());
        canvas.setSize({320, 320});
        QVERIFY(canvas.setProperty("bridge", QVariant::fromValue<QObject*>(&bridge)));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto snapshot = *bridge.canvasSnapshot();
        QTest::mousePress(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 30}, snapshot));
        QVERIFY(selectLayer(bridge, second));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {90, 30}, snapshot));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(1));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->back().layerId, nodeId(first));
        QTest::mousePress(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 60}, snapshot));
        QVERIFY(setVisible(bridge, second, false));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {90, 60}, snapshot));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(1));
        QVERIFY(setVisible(bridge, second, true));
        QTest::mousePress(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 90}, snapshot));
        bridge.createDocument(128, 128, 300);
        QTest::mouseRelease(&window, Qt::LeftButton, {}, canvasPoint(&canvas, {90, 90}, snapshot));
        QVERIFY(bridge.canvasSnapshot()->eventIndex->empty());
    }
    void treeOrderAndReadOnlyStates() {
        QtBridge bridge;
        bridge.createDocument(128, 128, 300);
        const auto first = selected(bridge);
        QVERIFY(addLayer(bridge));
        const auto second = selected(bridge);
        QVERIFY(selectLayer(bridge, first));
        QVERIFY(addLayer(bridge));
        const auto third = selected(bridge);
        QCOMPARE(rows(bridge).at(0).toMap().value("id").toString(), second);
        QCOMPARE(rows(bridge).at(1).toMap().value("id").toString(), third);
        QCOMPARE(rows(bridge).at(2).toMap().value("id").toString(), first);
        auto original = *bridge.canvasSnapshot();
        Resource visual;
        visual.id.bytes[0] = 42;
        visual.bytes = std::make_shared<const Bytes>(Bytes{255, 0, 0, 255});
        original.resources = std::make_shared<const ResourceMap>(ResourceMap{{visual.id, visual}});
        const auto parent = original.nodes->at(nodeId(first)).parent;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto openWithParent = [&](LayerNode node, const QString& name) {
            auto snapshot = original;
            auto nodes = std::make_shared<NodeMap>(*snapshot.nodes);
            nodes->at(parent) = std::move(node);
            snapshot.nodes = nodes;
            const auto file = QUrl::fromLocalFile(directory.filePath(name + ".draw"));
            if (!ProjectStore{}.save(std::filesystem::path(file.toLocalFile().toStdWString()), snapshot)) return false;
            bridge.openUrl(file);
            return bridge.canvasSnapshot()->sameContent(snapshot);
        };
        auto ancestor = original.nodes->at(parent);
        ancestor.locked = true;
        QVERIFY(openWithParent(ancestor, "locked"));
        QVERIFY(rows(bridge).front().toMap().value("locked").toBool());
        QVERIFY(!bridge.canAddLayer());
        QVERIFY(!bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(first), 200, 30)));
        ancestor = original.nodes->at(parent);
        ancestor.visible = false;
        QVERIFY(openWithParent(ancestor, "hidden"));
        QVERIFY(!bridge.canAddLayer());
        QVERIFY(!bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(first), 201, 30)));
        ancestor = original.nodes->at(parent);
        ancestor.transform.affine[4] = 4;
        QVERIFY(openWithParent(ancestor, "transformed"));
        QVERIFY(!rows(bridge).front().toMap().value("locked").toBool());
        QVERIFY(!bridge.canAddLayer());
        QVERIFY(!bridge.layerNotice().isEmpty());
        QVERIFY(!bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(first), 202, 30)));
        ancestor = original.nodes->at(parent);
        ancestor.opacity = .5;
        QVERIFY(openWithParent(ancestor, "unsupported-group-opacity"));
        QVERIFY(!bridge.layerNotice().isEmpty());
        QVERIFY(!bridge.canAddLayer());
        ancestor = original.nodes->at(parent);
        ancestor.content = {visual.id};
        QVERIFY(openWithParent(ancestor, "ancestor-content"));
        QVERIFY(!rows(bridge).front().toMap().value("supported").toBool());
        QVERIFY(!bridge.layerNotice().isEmpty());
        QVERIFY(!bridge.canAddLayer());
        QVERIFY(!bridge.commitStroke(stroke(*bridge.canvasSnapshot(), nodeId(first), 203, 30)));
        ancestor = original.nodes->at(parent);
        ancestor.sourceKind = SourceKind::ImportedRaster;
        ancestor.sourceResource = visual.id;
        QVERIFY(openWithParent(ancestor, "ancestor-imported-source"));
        QVERIFY(!rows(bridge).front().toMap().value("supported").toBool());
        QVERIFY(!bridge.layerNotice().isEmpty());
        QVERIFY(!bridge.canAddLayer());
    }
    void compositeUsesTreeOrderAndOpacityInsteadOfNodeIdOrSelection() {
        ApplicationController controller;
        NewDocumentRequest request;
        request.dimensions = {128, 128, Unit::Px, 300};
        QVERIFY(controller.newDocument(request));
        LayerNode object;
        object.id = Id::fromNumber(2); object.parent = Document::rootId();
        object.kind = NodeKind::Object; object.name = "Artwork"; object.bounds = {0, 0, 128, 128};
        LayerNode bottom;
        bottom.id = Id::fromNumber(100); bottom.parent = object.id; bottom.objectId = object.id;
        bottom.name = "Red bottom"; bottom.bounds = object.bounds;
        LayerNode top = bottom;
        top.id = Id::fromNumber(10); top.name = "Blue top"; top.opacity = .5;
        Command nodes;
        nodes.operations = {AddNode{object}, AddNode{bottom}, AddNode{top}};
        QVERIFY(controller.apply(nodes));
        for (const auto& [layer, y, number, color] : std::vector<std::tuple<NodeId, double, std::uint64_t, std::array<double, 4>>>{
            {bottom.id, 30, 300, {1, 0, 0, 0}}, {bottom.id, 70, 301, {1, 0, 0, 0}},
            {top.id, 70, 302, {0, 0, 1, 0}}, {top.id, 100, 303, {0, 0, 1, 0}}}) {
            const auto event = stroke(*controller.snapshot(), layer, number, y, color);
            QVERIFY(controller.apply(Command{Paint{event, {20, std::int64_t(y) - 8, 80, 16}}}));
        }
        QTemporaryDir directory;
        const auto file = directory.filePath("colored-layers.draw");
        QVERIFY(controller.save(std::filesystem::path(file.toStdWString())));
        QtBridge bridge;
        bridge.openUrl(QUrl::fromLocalFile(file));
        QQuickWindow window;
        window.resize(320, 320);
        CanvasItem canvas(window.contentItem());
        canvas.setSize({320, 320});
        QVERIFY(canvas.setProperty("bridge", QVariant::fromValue<QObject*>(&bridge)));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(matchesReference(&window, &canvas, *bridge.canvasSnapshot(), {60, 30}));
        QTRY_VERIFY(matchesReference(&window, &canvas, *bridge.canvasSnapshot(), {60, 70}));
        QTRY_VERIFY(matchesReference(&window, &canvas, *bridge.canvasSnapshot(), {60, 100}));
        const auto overlap = pixel(&window, &canvas, *bridge.canvasSnapshot(), {60, 70});
        QVERIFY(std::abs(overlap.red() - 128) < 5 && overlap.green() < 5 && std::abs(overlap.blue() - 128) < 5);
        QVERIFY(selectLayer(bridge, QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bottom.id.bytes.data()), 16).toHex())));
        QCOMPARE(pixel(&window, &canvas, *bridge.canvasSnapshot(), {60, 70}), overlap);
        const auto topText = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(top.id.bytes.data()), 16).toHex());
        QVERIFY(setVisible(bridge, topText, false));
        QTRY_VERIFY(pixel(&window, &canvas, *bridge.canvasSnapshot(), {60, 70}).red() > 250);
        QTRY_VERIFY(pixel(&window, &canvas, *bridge.canvasSnapshot(), {60, 70}).blue() < 5);
        SetProperties translated;
        translated.id = bottom.id;
        translated.transform = LayerTransform{};
        translated.transform->affine[4] = 64;
        QVERIFY(controller.apply(Command{translated}));
        const auto transformedFile = directory.filePath("translated-layer.draw");
        QVERIFY(controller.save(std::filesystem::path(transformedFile.toStdWString())));
        bridge.openUrl(QUrl::fromLocalFile(transformedFile));
        window.resize(480, 320);
        canvas.setSize({480, 320});
        QTRY_VERIFY(pixel(&window, &canvas, *bridge.canvasSnapshot(), {110, 30}).red() > 250);
        QTRY_VERIFY(pixel(&window, &canvas, *bridge.canvasSnapshot(), {110, 30}).green() < 5);
        // The translated stroke crosses the page edge; the letterbox must stay gray.
        QTRY_COMPARE(pixel(&window, &canvas, *bridge.canvasSnapshot(), {135, 30}), QColor("#323235"));
    }
    void qmlButtonsMousePaintCompositeAndReopen() {
        QtBridge bridge;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("studio", &bridge);
        engine.load(QUrl::fromLocalFile(QStringLiteral(DRAWBORAD_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY2(clickItem(window, window, "newDocumentButton"), "New must be reachable in the real QML window");
        auto dialog = window->findChild<QObject*>("newDocumentDialog");
        QVERIFY(dialog);
        auto width = window->findChild<QObject*>("documentWidth");
        auto height = window->findChild<QObject*>("documentHeight");
        QVERIFY(width && height);
        QVERIFY(width->setProperty("text", "128"));
        QVERIFY(height->setProperty("text", "128"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(bridge.hasDocument());
        auto canvas = window->findChild<QQuickItem*>("nativeCanvas");
        auto list = window->findChild<QObject*>("layerList");
        QVERIFY(canvas && list);
        const auto first = selected(bridge);
        draw(window, canvas, *bridge.canvasSnapshot(), 30);
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(1));
        QVERIFY(clickItem(window, window, "addLayerButton"));
        const auto second = selected(bridge);
        QVERIFY(first != second);
        QTRY_COMPARE(list->property("count").toInt(), 2);
        draw(window, canvas, *bridge.canvasSnapshot(), 70);
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(2));
        QVERIFY(bridge.canvasSnapshot()->eventIndex->at(0).id != bridge.canvasSnapshot()->eventIndex->at(1).id);
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 30}));
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 70}));
        QVERIFY(clickItem(window, window, "layerRow_" + first));
        QCOMPARE(selected(bridge), first);
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 70}));
        QVERIFY(clickItem(window, window, "layerVisibility_" + second));
        QTRY_VERIFY(pixel(window, canvas, *bridge.canvasSnapshot(), {60, 70}).red() > 220);
        QVERIFY(clickItem(window, window, "layerVisibility_" + second));
        draw(window, canvas, *bridge.canvasSnapshot(), 100);
        const auto expected = *bridge.canvasSnapshot();
        QCOMPARE(expected.eventIndex->size(), std::size_t(3));
        QCOMPARE(expected.eventIndex->at(0).layerId, nodeId(first));
        QCOMPARE(expected.eventIndex->at(1).layerId, nodeId(second));
        QCOMPARE(expected.eventIndex->at(2).layerId, nodeId(first));
        QTemporaryDir directory;
        const auto file = QUrl::fromLocalFile(directory.filePath("ui-two-layers.draw"));
        bridge.saveUrl(file);
        QVERIFY(!bridge.dirty());
        bridge.createDocument(128, 128, 300);
        bridge.openUrl(file);
        QVERIFY(bridge.canvasSnapshot()->sameContent(expected));
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 30}));
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 70}));
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 100}));
        QVERIFY(selectLayer(bridge, first));
        draw(window, canvas, *bridge.canvasSnapshot(), 45);
        QVERIFY(selectLayer(bridge, second));
        draw(window, canvas, *bridge.canvasSnapshot(), 85);
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->size(), std::size_t(5));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->at(3).layerId, nodeId(first));
        QCOMPARE(bridge.canvasSnapshot()->eventIndex->at(4).layerId, nodeId(second));
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 45}));
        QTRY_VERIFY(matchesReference(window, canvas, *bridge.canvasSnapshot(), {60, 85}));
        bridge.saveUrl(file);
        QVERIFY(!bridge.dirty());
        const auto artifact = qEnvironmentVariable("DRAWBORAD_TEST_ARTIFACT_DIR");
        if (!artifact.isEmpty()) {
            QVERIFY(QDir().mkpath(artifact));
            QVERIFY(window->grabWindow().save(artifact + "/layers-reopened.png"));
            QFile::remove(artifact + "/layers-smoke.draw");
            QVERIFY(QFile::copy(file.toLocalFile(), artifact + "/layers-smoke.draw"));
        }
        qInfo() << "PASS actual QML add/select/visibility, mouse strokes, two visible layers and save/reopen";
        window->setProperty("approvedClose", true);
        window->close();
    }
};
int main(int argc, char** argv) {
    // Keep picker callback tests deterministic without native COM dialog threads.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication application(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    qmlRegisterType<CanvasItem>("Drawborad.Native", 1, 0, "NativeCanvas");
    LayerUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "LayerUiTests.moc"

