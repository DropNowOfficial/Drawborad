#include "QtBridge.h"
#include "AlphaImportPolicy.h"
#include "storage/ProjectCodec.h"
#include "render/ReferenceRenderer.h"
#include "view/LayerTree.h"
#include "ui/WorkflowController.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUuid>
#include <algorithm>
namespace drawborad {
namespace {
Id uuid() {
    const auto b = QUuid::createUuid().toRfc4122();
    Id id;
    for (int i = 0; i < 16; i++) id.bytes[std::size_t(i)] = std::uint8_t(b[i]);
    return id;
}
QString layerText(NodeId id) {
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.bytes.data()), 16).toHex());
}
std::optional<NodeId> parseLayerId(const QString& text) {
    if (text.size() != 32) return std::nullopt;
    const auto bytes = QByteArray::fromHex(text.toLatin1());
    if (bytes.size() != 16 || QString::fromLatin1(bytes.toHex()) != text.toLower()) return std::nullopt;
    NodeId id;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()), 16, id.bytes.begin());
    return id;
}
}
QtBridge::QtBridge(QObject* parent)
    : QObject(parent),
      host_(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / L"DrawboradWorker.exe"),
      broker_(host_, governor_, clock_) {
    workflow_=new WorkflowController(*this);
    connect(workflow_,&WorkflowController::changed,this,&QtBridge::changed);
    poll_.setInterval(25);
    connect(&poll_, &QTimer::timeout, this, [this] {
        broker_.poll();
        if (!running_) return;
        auto state = broker_.status(*running_);
        if (state == JobStatus::Cancelled || state == JobStatus::Failed) {
            running_.reset(); workerMessage_ = tr("Worker stopped. Document unchanged."); emit changed(); return;
        }
        auto m = host_.pollMessage(*running_);
        if (!m) { broker_.workerFailed(*running_); running_.reset(); workerMessage_=tr("Worker failed safely."); emit changed(); return; }
        if (m.value()) {
            if (m.value()->kind == MessageKind::Hello) { workerMessage_=tr("Native worker connected. You can move the window or cancel."); emit changed(); }
            if (m.value()->kind == MessageKind::Cancelled) { broker_.acknowledgeCancel(m.value()->id,m.value()->nonce); running_.reset(); workerMessage_=tr("Cancelled. Document unchanged."); emit changed(); }
        }
    });
    poll_.start();
}
bool QtBridge::dirty() const {return app_.dirty() || workflow_->pending();}
int QtBridge::eventCount() const {const auto s=app_.snapshot();return s?int(std::min<std::size_t>(s->eventIndex->size(),std::size_t(INT_MAX))):0;}
int QtBridge::replayCount() const {return replayCount_<0?eventCount():std::min(replayCount_,eventCount());}
void QtBridge::setReplayCount(int count){count=std::clamp(count,0,eventCount());replayCount_=count==eventCount()?-1:count;emit changed();}
bool QtBridge::undoDrawing(){
    if(busy() || workflow_->pending() || !app_.undo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::redoDrawing(){
    if(busy() || workflow_->pending() || !app_.redo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::busy() const {return running_.has_value() || workflow_->busy();}
QObject* QtBridge::workflow() const {return workflow_;}
QString QtBridge::message() const { return !app_.message().empty()?QString::fromStdString(app_.message()):workerMessage_; }
QString QtBridge::dimensions() const { auto s=app_.snapshot(); if(!s)return tr("No document"); return QString::number(s->spec.pixelSize.width)+" x "+QString::number(s->spec.pixelSize.height)+tr(" px / revision ")+QString::number(s->revision); }
QVariantList QtBridge::layers() const {
    QVariantList result;
    const auto snapshot = app_.snapshot();
    if (!snapshot) return result;
    const auto order = layerOrder(*snapshot);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto& node = snapshot->nodes->at(*it);
        result.push_back(QVariantMap{{"id", layerText(*it)}, {"name", QString::fromStdString(node.name)},
            {"visible", node.visible}, {"locked", layerLocked(*snapshot, *it)},
            {"supported", nativeLayerSupported(*snapshot, *it)},
            {"paintable", nativeLayerPaintable(*snapshot, *it)}});
    }
    return result;
}
QString QtBridge::activeLayerId() const { return activeLayer_ ? layerText(*activeLayer_) : QString{}; }
void QtBridge::resetLayerSelection() {
    activeLayer_.reset();
    const auto snapshot = app_.snapshot();
    if (!snapshot) return;
    const auto order = layerOrder(*snapshot);
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if (nativeLayerPaintable(*snapshot, *it)) { activeLayer_ = *it; return; }
    if (!order.empty()) activeLayer_ = order.back();
}
bool QtBridge::canAddLayer() const {
    const auto snapshot = app_.snapshot();
    if (!snapshot || snapshot->spec.colorSpec.format != PixelFormat{ColorMode::RGB, Depth::U8, true}) return false;
    auto parent = Document::rootId();
    if (activeLayer_ && snapshot->nodes->contains(*activeLayer_)) parent = snapshot->nodes->at(*activeLayer_).parent;
    return layerWritable(*snapshot, parent) && layerVisible(*snapshot, parent) &&
        nativeCompositingSupported(*snapshot, parent);
}
QString QtBridge::layerNotice() const {
    const auto snapshot = app_.snapshot();
    if (!snapshot) return {};
    for (const auto& [id, node] : *snapshot->nodes)
        if (layerVisible(*snapshot, id) &&
            (node.kind == NodeKind::Layer ? !nativeLayerSupported(*snapshot, id)
                                          : !nativeCompositingSupported(*snapshot, id)))
            return tr("Some content cannot be displayed by this canvas.");
    if (!activeLayer_ || !nativeLayerPaintable(*snapshot, *activeLayer_))
        return tr("Select an editable, visible paint layer to draw.");
    return {};
}
bool QtBridge::selectLayer(const QString& text) {
    const auto snapshot = app_.snapshot();
    const auto id = parseLayerId(text);
    if (!snapshot || !id || !snapshot->nodes->contains(*id) || snapshot->nodes->at(*id).kind != NodeKind::Layer)
        return false;
    if (activeLayer_ == id) return true;
    activeLayer_ = id;
    emit changed();
    return true;
}
bool QtBridge::addLayer() {
    const auto snapshot = app_.snapshot();
    if (!snapshot || !canAddLayer()) return false;
    Command command;
    NodeId parent, objectId;
    std::size_t index = SIZE_MAX;
    if (activeLayer_ && snapshot->nodes->contains(*activeLayer_)) {
        const auto& selected = snapshot->nodes->at(*activeLayer_);
        parent = selected.parent;
        objectId = selected.objectId;
        const auto& siblings = snapshot->nodes->at(parent).children;
        index = std::size_t(std::find(siblings.begin(), siblings.end(), *activeLayer_) - siblings.begin()) + 1;
    }
    if (objectId.empty()) {
        LayerNode object;
        object.id = uuid(); object.parent = Document::rootId(); object.kind = NodeKind::Object;
        object.name = "Artwork";
        object.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
        parent = objectId = object.id;
        command.operations.push_back(AddNode{object});
        index = SIZE_MAX;
    }
    LayerNode layer;
    layer.id = uuid(); layer.parent = parent; layer.objectId = objectId;
    layer.name = tr("Layer %1").arg(qulonglong(layerOrder(*snapshot).size() + 1)).toUtf8().toStdString();
    layer.stage = "paint";
    layer.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
    command.operations.push_back(AddNode{layer, index});
    const auto result = app_.apply(command);
    if (result) activeLayer_ = layer.id;
    emit changed();
    return bool(result);
}
bool QtBridge::setLayerVisible(const QString& text, bool visible) {
    const auto snapshot = app_.snapshot();
    const auto id = parseLayerId(text);
    if (!snapshot || !id || !snapshot->nodes->contains(*id) || snapshot->nodes->at(*id).kind != NodeKind::Layer)
        return false;
    if (snapshot->nodes->at(*id).visible == visible) return true;
    SetProperties properties;
    properties.id = *id; properties.visible = visible;
    const auto result = app_.apply(Command{properties});
    emit changed();
    return bool(result);
}
bool QtBridge::initializeCanvas(double w, double h, double ppi) {
    NewDocumentRequest request;
    request.dimensions = {w, h, Unit::Px, ppi};
    if (!app_.newDocument(request)) return false;
    const auto snapshot = app_.snapshot();
    if (!snapshot) return false;
    const auto objectId = uuid(), layerId = uuid();
    LayerNode object;
    object.id = objectId;
    object.parent = Document::rootId();
    object.kind = NodeKind::Object;
    object.name = "Artwork";
    object.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
    LayerNode layer;
    layer.id = layerId;
    layer.parent = objectId;
    layer.objectId = objectId;
    layer.kind = NodeKind::Layer;
    layer.name = "Paint";
    layer.stage = "paint";
    layer.bounds = object.bounds;
    Command command;
    command.operations = {AddNode{object}, AddNode{layer}};
    const auto created = app_.apply(command);
    if (created) { replayCount_=-1; activeLayer_ = layerId; ++canvasGeneration_; }
    return bool(created);
}
void QtBridge::createDocument(double w, double h, double ppi) {
    const bool created = initializeCanvas(w, h, ppi);
    if (created) {
        workflow_->reset();
        referenceUrl_ = {};
        referenceFeedback_.clear();
    } else {
        referenceFeedback_ = tr("Could not create the canvas. Check the dimensions.");
    }
    emit changed();
}
bool QtBridge::importReferenceUrl(const QUrl& url) {
    if(busy() || workflow_->pending())return false;
    if (!url.isLocalFile()) {
        referenceFeedback_ = tr("Only local image files are supported.");
        emit changed();
        return false;
    }
    const QFileInfo file(url.toLocalFile());
    const auto suffix = file.suffix().toLower();
    if (!file.isFile() || !file.isReadable() ||
        (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") &&
         suffix != QLatin1String("jpeg"))) {
        referenceFeedback_ = tr("Choose a readable PNG or JPEG image.");
        emit changed();
        return false;
    }
    const auto encodedSize = file.size();
    QImageReader reader(file.filePath());
    reader.setAutoTransform(true);
    const QSize imageSize = reader.size();
    const AlphaReferenceCandidate preliminary{
        true, true, encodedSize > 0 ? std::uint64_t(encodedSize) : 0,
        imageSize.width() > 0 ? std::uint32_t(imageSize.width()) : 0,
        imageSize.height() > 0 ? std::uint32_t(imageSize.height()) : 0};
    auto safe = validateAlphaReferenceCandidate(preliminary);
    if (!safe) {
        referenceFeedback_ = tr("Reference image must be PNG/JPEG, non-empty, <= 32 MB and <= 4096 px on each side.");
        emit changed();
        return false;
    }
    const QImage decoded = reader.read();
    safe = validateAlphaReferenceCandidate({true, !decoded.isNull(), preliminary.encodedBytes,
        decoded.width() > 0 ? std::uint32_t(decoded.width()) : 0,
        decoded.height() > 0 ? std::uint32_t(decoded.height()) : 0});
    if (!safe) {
        referenceFeedback_ = tr("Could not decode the image within the Alpha safety limits.");
        emit changed();
        return false;
    }
    const auto existing = app_.snapshot();
    const bool sameCanvas = existing &&
        existing->spec.pixelSize.width == std::uint64_t(decoded.width()) &&
        existing->spec.pixelSize.height == std::uint64_t(decoded.height());
    if (!sameCanvas && !initializeCanvas(decoded.width(), decoded.height(), 300.0)) {
        referenceFeedback_ = tr("Failed to create the reference canvas.");
        emit changed();
        return false;
    }
    referenceUrl_ = url;
    referenceFeedback_ = sameCanvas
        ? tr("Reference preview attached without changing existing strokes. The .draw file does not embed the image.")
        : tr("New canvas created from the image. The .draw file saves strokes, not the reference image.");
    emit changed();
    return true;
}
Result<void> QtBridge::commitStroke(const StrokeEvent& event) {
    const auto snapshot = app_.snapshot();
    if (!snapshot || busy() || replaying() || !nativeLayerPaintable(*snapshot, event.layerId))
        return error(ErrorCode::InvalidInput, "paint.layer-unavailable");
    const auto bounds = strokeBounds(event);
    if (!bounds) return bounds.error();
    const auto committed = app_.apply(Command{Paint{event, bounds.value()}});
    if (!committed) return committed.error();
    emit changed();
    return {};
}
void QtBridge::openUrl(const QUrl& p){
    if(!p.isLocalFile())return;
    auto done=app_.open(std::filesystem::path(p.toLocalFile().toStdWString()));
    if(done){replayCount_=-1;referenceUrl_={};referenceFeedback_=tr("Reference previews are not stored in .draw projects. Reimport the source image to compare.");++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());}
    emit changed();
}
void QtBridge::saveUrl(const QUrl& p){if(!p.isLocalFile())return;if(workflow_->pending()){workflow_->fail(tr("请先接受审阅蒙版，再保存工程。未接受的修改仍然保留；也可取消关闭或明确放弃。" ));return;}auto done=app_.save(std::filesystem::path(p.toLocalFile().toStdWString()));(void)done;emit changed();}
void QtBridge::probeWorker(){if(running_||!app_.snapshot())return;auto s=*app_.snapshot();auto bytes=encodeDelta(DocumentSnapshot{},s);auto hash=stateHash(s);if(!bytes||!hash)return;JobRequest q;q.id=uuid();q.nonce=uuid();q.task="native.smoke";q.inputSnapshot={s.documentId,s.revision,hash.value()};q.snapshotBytes=std::move(bytes.value());q.resources={q.id,ResourceDevice::CPU,16*1024*1024,0,0,false};auto id=broker_.submit(q);if(id){running_=id.value();workerMessage_=tr("Connecting native worker...");}else workerMessage_=QString::fromStdString(id.error().messageKey);emit changed();}
void QtBridge::cancelWorker(){if(workflow_->busy())workflow_->cancel();if(running_){broker_.cancel(*running_);workerMessage_=tr("Cancelling worker...");emit changed();}}
}
