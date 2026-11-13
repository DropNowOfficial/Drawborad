#include "QtBridge.h"
#include "AlphaImportPolicy.h"
#include "brush/OriginalLineartFill.h"
#include "ui/MergedDocument.h"
#include <QFile>
#include <QColorSpace>
#include <QBuffer>
#include <cmath>
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
        referenceUrl_ = QUrl{};
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
bool QtBridge::importLineartUrl(const QUrl& url) try {
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    auto s=app_.snapshot();
    if(!s || busy() || replaying() || workflow_->pending())return fail(tr("请先结束当前操作并回到完整工程。"));
    if(!s->eventIndex->empty() || !s->resources->empty())return fail(tr("仅向空白同尺寸工程嵌入线稿；现有作品未修改。"));
    const auto& children=s->nodes->at(Document::rootId()).children;
    if(!children.empty()){
        if(children.size()!=1 || s->nodes->size()!=3)return fail(tr("请使用新建的默认空白工程；现有图层结构保留不变。"));
        const auto& object=s->nodes->at(children.front());
        if(object.kind!=NodeKind::Object || object.name!="Artwork" || object.children.size()!=1)return fail(tr("现有自定义图层结构不会被覆盖。"));
        const auto unchanged=[](const LayerNode& n){return !n.locked && n.visible && n.opacity==1 && n.blend==BlendMode::Normal && n.maskId.empty() && n.transform==LayerTransform{} && n.content.empty();};
        if(!unchanged(object))return fail(tr("现有自定义图层属性不会被覆盖。"));
        const auto& layer=s->nodes->at(object.children.front());
        if(!unchanged(layer) || layer.name!="Paint" || layer.stage!="paint" || layer.kind!=NodeKind::Layer || !layer.content.empty())return fail(tr("现有自定义图层结构不会被覆盖。"));
    }
    if(s->spec.pixelSize.width>4096 || s->spec.pixelSize.height>4096 || s->spec.pixelSize.width*s->spec.pixelSize.height>4*1024*1024)return fail(tr("当前桌面填色上限为 4 Mi 像素、单边 4096；不会缩小图片。大海报尚未接通。"));
    if(s->spec.colorSpec.format.mode!=ColorMode::RGB || s->spec.colorSpec.format.depth!=Depth::U8 || !s->spec.colorSpec.profileId.empty())return fail(tr("当前线稿导入仅支持未指定 ICC 的 RGB 8 位工程。"));
    if(!url.isLocalFile())return fail(tr("请选择本地 PNG 线稿。"));
    QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly) || file.size()<=0 || file.size()>32*1024*1024)return fail(tr("PNG 文件须可读且不超过 32 MiB。"));
    const auto expectedSize=file.size();const auto encoded=file.read(32*1024*1024+1);
    if(encoded.size()!=expectedSize || !file.atEnd())return fail(tr("读取 PNG 时文件发生变化或读取不完整，未导入。"));
    QBuffer buffer;buffer.setData(encoded);if(!buffer.open(QIODevice::ReadOnly))return fail(tr("无法读取 PNG 缓冲。"));
    QImageReader reader(&buffer,"png");reader.setAutoTransform(false);const auto size=reader.size();
    if(size.width()!=int(s->spec.pixelSize.width) || size.height()!=int(s->spec.pixelSize.height))return fail(tr("线稿必须与画布像素尺寸一致；不会自动放大或缩小。"));
    auto image=reader.read();if(image.isNull())return fail(tr("PNG 解码失败，工程未修改。"));
    if(image.colorSpace().isValid())return fail(tr("带 ICC/色彩空间标签的线稿尚未接通转换，未修改工程。"));
    image=image.convertToFormat(QImage::Format_RGBA8888);
    if(image.isNull() || image.size()!=size)return fail(tr("线稿像素转换失败，工程未修改。"));
    Bytes rgba(std::size_t(image.width())*image.height()*4);
    for(int y=0;y<image.height();y++)std::copy_n(image.constScanLine(y),std::size_t(image.width())*4,rgba.begin()+std::size_t(y)*image.width()*4);
    for(std::size_t i=0;i<rgba.size();i+=4)if(rgba[i+3] && (rgba[i]!=rgba[i+1] || rgba[i+1]!=rgba[i+2]))return fail(tr("此入口只接受黑白灰线稿，不会把彩色成图自动转成线稿。"));
    const auto ink=uuid(),fill=uuid();auto import=makeLineartRasterCommand(std::move(rgba),Bytes(encoded.begin(),encoded.end()),image.width(),image.height(),ink);if(!import)return fail(QString::fromStdString(import.error().messageKey));
    Command command;for(auto id:s->nodes->at(Document::rootId()).children)command.operations.push_back(RemoveNode{id});
    LayerNode layer;layer.id=fill;layer.parent=Document::rootId();layer.name="Color fill";layer.bounds={0,0,s->spec.pixelSize.width,s->spec.pixelSize.height};command.operations.push_back(AddNode{layer});
    command.operations.insert(command.operations.end(),import.value().operations.begin(),import.value().operations.end());
    auto candidate=Document::fromSnapshot(*s);if(!candidate)return fail(QString::fromStdString(candidate.error().messageKey));auto applied=candidate.value().apply(command,s->revision);if(!applied)return fail(QString::fromStdString(applied.error().messageKey));auto plan=planMergedDocument(candidate.value().snapshot());if(!plan)return fail(QString::fromStdString(plan.error().messageKey));
    auto result=app_.apply(command);if(!result)return fail(QString::fromStdString(result.error().messageKey));
    activeLayer_=fill;++canvasGeneration_;referenceUrl_=QUrl{};fillNotice_=tr("原 PNG 已嵌入并锁定；色块写入独立底层。仅按原线稿取样，不重绘、不自动补洞。");emit changed();return true;
}catch(const std::bad_alloc&){fillNotice_=tr("内存不足，线稿未导入。");emit changed();return false;}
bool QtBridge::fillLineartAt(double x,double y,const QColor& color) {
    auto s=app_.snapshot();
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    if(!s || busy() || replaying() || workflow_->pending() || !activeLayer_ || !color.isValid() || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=double(s->spec.pixelSize.width) || y>=double(s->spec.pixelSize.height))return fail(tr("填色未执行：请选可编辑色层并结束当前操作。"));
    NodeId source;for(const auto&[id,node]:*s->nodes)if(node.stage==LineartStage){if(!source.empty())return fail(tr("目前只支持一个原线稿图层。"));source=id;}
    ColorValue paint;paint.activeChannels=3;paint.profileId=s->spec.colorSpec.profileId;paint.channels={color.redF(),color.greenF(),color.blueF(),0};paint.alpha=color.alphaF();
    auto command=originalLineartFillCommand(*s,source,*activeLayer_,uuid(),std::uint32_t(x),std::uint32_t(y),paint);
    if(!command)return fail(tr("未填色，工程未修改：")+QString::fromStdString(command.error().messageKey));
    auto result=app_.apply(command.value());if(!result)return fail(QString::fromStdString(result.error().messageKey));
    fillNotice_=tr("已按原线稿闭区写入独立色层；可撤销，原线稿字节未修改。");emit changed();return true;
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
    if(done){replayCount_=-1;referenceUrl_=QUrl{};referenceFeedback_=tr("Reference previews are not stored in .draw projects. Reimport the source image to compare.");++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());}
    emit changed();
}
void QtBridge::saveUrl(const QUrl& p){if(!p.isLocalFile())return;if(workflow_->pending()){workflow_->fail(tr("请先接受审阅蒙版，再保存工程。未接受的修改仍然保留；也可取消关闭或明确放弃。" ));return;}auto done=app_.save(std::filesystem::path(p.toLocalFile().toStdWString()));(void)done;emit changed();}
void QtBridge::probeWorker(){if(running_||!app_.snapshot())return;auto s=*app_.snapshot();auto bytes=encodeDelta(DocumentSnapshot{},s);auto hash=stateHash(s);if(!bytes||!hash)return;JobRequest q;q.id=uuid();q.nonce=uuid();q.task="native.smoke";q.inputSnapshot={s.documentId,s.revision,hash.value()};q.snapshotBytes=std::move(bytes.value());q.resources={q.id,ResourceDevice::CPU,16*1024*1024,0,0,false};auto id=broker_.submit(q);if(id){running_=id.value();workerMessage_=tr("Connecting native worker...");}else workerMessage_=QString::fromStdString(id.error().messageKey);emit changed();}
void QtBridge::cancelWorker(){if(workflow_->busy())workflow_->cancel();if(running_){broker_.cancel(*running_);workerMessage_=tr("Cancelling worker...");emit changed();}}
}

