#include "QtBridge.h"
#include "AlphaImportPolicy.h"
#include "core/OriginalReference.h"
#include "brush/OriginalLineartFill.h"
#include "brush/FillRegionReview.h"
#include "brush/HollowInkCommand.h"
#include "brush/DerivedHollowFill.h"
#include "brush/ClipFillSelection.h"
#include "brush/ClipPolygonSelection.h"
#include "brush/CloseLineartGap.h"
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
QtBridge::~QtBridge(){fillStop_.request_stop();fillPool_.clear();fillPool_.waitForDone();}
void QtBridge::cancelFill(){if(fillPending()){pendingFill_.reset();pendingFillPreview_.reset();pendingFillSource_.reset();fillNotice_=tr("审阅已取消，原工程未修改。");emit changed();return;}if(fillBusy_){fillStop_.request_stop();fillNotice_=tr("正在取消区域操作；原图及已提交内容不变。");emit changed();}}
bool QtBridge::acceptFill(){
    if(!pendingFill_ || !pendingFillSource_)return false;
    const auto current=app_.snapshot();const auto source=*pendingFillSource_;
    if(!current || current->documentId!=source.documentId || current->revision!=source.revision || canvasGeneration_!=pendingFillGeneration_ || replaying()){cancelFill();fillNotice_=tr("工程发生变化，审阅结果未提交。");emit changed();return false;}
    auto command=std::move(*pendingFill_);pendingFill_.reset();pendingFillPreview_.reset();pendingFillSource_.reset();
    std::optional<NodeId> paintedLayer;for(const auto& op:command.operations)if(const auto* p=std::get_if<Paint>(&op))paintedLayer=p->event.layerId;
    auto committed=app_.apply(command);if(committed&&paintedLayer)activeLayer_=paintedLayer;fillNotice_=committed?tr("已接受区域操作，独立图层已更新；原线稿未改，可撤销和保存。"):QString::fromStdString(committed.error().messageKey);emit changed();return bool(committed);
}
bool QtBridge::dirty() const {return app_.dirty() || workflow_->pending() || fillPending();}
int QtBridge::eventCount() const {const auto s=app_.snapshot();return s?int(std::min<std::size_t>(s->eventIndex->size(),std::size_t(INT_MAX))):0;}
int QtBridge::replayCount() const {return replayCount_<0?eventCount():std::min(replayCount_,eventCount());}
void QtBridge::setReplayCount(int count){if(strokeCommitBusy_ || fillBusy_ || fillPending())return;count=std::clamp(count,0,eventCount());replayCount_=count==eventCount()?-1:count;emit changed();}
bool QtBridge::undoDrawing(){
    if(busy() || workflow_->pending() || !app_.undo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::redoDrawing(){
    if(busy() || workflow_->pending() || !app_.redo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::busy() const {return strokeCommitBusy_ || fillBusy_ || fillPending() || running_.has_value() || workflow_->busy();}
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
    if(busy() || workflow_->pending())return false;
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
    if(busy() || workflow_->pending())return false;
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
bool QtBridge::initializeCanvas(double w, double h, double ppi, bool whitePaper) {
    if(busy())return false;
    NewDocumentRequest request;
    request.dimensions = {w, h, Unit::Px, ppi};
    if(whitePaper){request.transparent=false;request.paperColor.channels={1,1,1,0};request.paperColor.alpha=1;}
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
void QtBridge::createDocument(double w, double h, double ppi, bool whitePaper) {
    const bool created = initializeCanvas(w, h, ppi, whitePaper);
    if (created) {
        workflow_->reset();
        referenceUrl_ = QUrl{};
        referenceFeedback_.clear();
    } else {
        referenceFeedback_ = tr("Could not create the canvas. Check the dimensions.");
    }
    emit changed();
}
namespace {
QImage decodeOriginalReference(const QByteArray& bytes) {
 QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setAutoTransform(true);
 const auto fmt=reader.format();const auto size=reader.size();
 if((fmt!="png"&&fmt!="jpeg")||size.width()<=0||size.height()<=0||size.width()>8192||size.height()>8192||qint64(size.width())*size.height()>20000000)return {};
 auto image=reader.read();if(image.isNull()||image.width()>8192||image.height()>8192||qint64(image.width())*image.height()>20000000)return {};
 return image;
}
}
void QtBridge::refreshOriginalReference() const try {
 const auto s=app_.snapshot();const auto* map=s?s->resources.get():nullptr;if(map==referenceCacheResources_)return;
 const auto oldKey=referenceCacheKey_;const auto oldImage=referenceCacheImage_;const auto oldInfo=referenceCacheInfo_;
 referenceCacheResources_=map;referenceCacheOwner_=s?s->resources:nullptr;referenceCacheKey_.clear();referenceCacheInfo_.clear();referenceCacheImage_={};if(!s){referenceCacheBytes_.reset();return;}
 auto ref=originalReference(*s,false);if(!ref){referenceCacheInfo_=tr("原彩附件损坏，不能显示；未改动画作。");return;}if(!ref.value()){referenceCacheBytes_.reset();return;}
 const auto& r=*ref.value();const auto key=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.encoded.bytes.data()),32).toHex());
 if(key==oldKey&&s->resources->at(r.encoded).bytes==referenceCacheBytes_&&oldImage.width()==int(r.width)&&oldImage.height()==int(r.height)&&!oldImage.isNull()){referenceCacheKey_=oldKey;referenceCacheImage_=oldImage;referenceCacheInfo_=oldInfo;return;}
 if(!originalReference(*s)){referenceCacheInfo_=tr("原彩附件哈希不匹配；未改动画作。");return;}
 const auto& b=*s->resources->at(r.encoded).bytes;
 auto image=decodeOriginalReference(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
 if(image.isNull()||image.width()!=int(r.width)||image.height()!=int(r.height)){referenceCacheInfo_=tr("原彩附件尺寸/解码不匹配；未改动画作。");return;}
 const bool profile=image.colorSpace().isValid();if(profile)image.convertToColorSpace(QColorSpace::SRgb);
 if(image.isNull()){referenceCacheInfo_=tr("原彩屏幕色彩转换失败；工程字节仍保留。");return;}
 referenceCacheBytes_=s->resources->at(r.encoded).bytes;referenceCacheImage_=image;referenceCacheKey_=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.encoded.bytes.data()),32).toHex());
 referenceCacheInfo_=tr("原彩参考 %1 × %2 px；显示缩放不增加细节。%3 原始文件字节随工程保存，导出画作不含参考；不是生成作品或作者证明。") .arg(r.width).arg(r.height).arg(profile?tr("屏幕按 sRGB 显示，非印刷校色。 "):tr("原图无已识别色彩配置，颜色未校准。 "));
}catch(const std::bad_alloc&){referenceCacheImage_={};referenceCacheKey_.clear();referenceCacheInfo_=tr("原彩显示内存不足；工程内容未因此修改。");}
QUrl QtBridge::originalReferenceUrl() const {refreshOriginalReference();return referenceCacheKey_.isEmpty()?QUrl{}:QUrl("image://original-reference/"+referenceCacheKey_);}
QString QtBridge::originalReferenceInfo() const {refreshOriginalReference();return referenceCacheInfo_;}
QImage QtBridge::originalReferenceImage(const QString& key) const {refreshOriginalReference();return key==referenceCacheKey_?referenceCacheImage_:QImage{};}
QVariantMap QtBridge::sampleOriginalReference(const QString& expectedUrl,double x,double y) const {
 const auto url=originalReferenceUrl();if(url.isEmpty()||expectedUrl!=url.toString()||!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=referenceCacheImage_.width()||y>=referenceCacheImage_.height())return {{"valid",false}};
 const int px=int(std::floor(x)),py=int(std::floor(y));auto color=referenceCacheImage_.pixelColor(px,py);const double alpha=color.alphaF();color.setAlpha(255);
 return {{"valid",true},{"color",color},{"hex",color.name()},{"alpha",alpha},{"x",px},{"y",py},{"source",url.toString()}};
}
bool QtBridge::attachOriginalReferenceUrl(const QUrl& url) try {
 const auto fail=[&](const QString& message){originalReferenceNotice_=message;emit changed();return false;};
 if(busy()||workflow_->pending()||replaying()||!app_.snapshot())return fail(tr("请先结束预览/回放并打开工程；原图未改变。"));
 if(!url.isLocalFile())return fail(tr("只接受本地 PNG/JPEG。"));
 QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>32*1024*1024)return fail(tr("原彩文件须在 32 MiB 以内。"));
 const auto expectedSize=file.size();const auto bytes=file.read(32*1024*1024+1);if(bytes.size()!=expectedSize||file.error()!=QFileDevice::NoError||bytes.isEmpty()||bytes.size()>32*1024*1024||!file.atEnd())return fail(tr("读取失败或超过预算。"));
 const auto image=decodeOriginalReference(bytes);if(image.isNull())return fail(tr("需可解码 PNG/JPEG，单边 ≤8192、总像素 ≤2000 万；不会偷偷缩小。"));
 Bytes encoded(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());
 const auto s=*app_.snapshot();auto existing=originalReference(s);if(!existing)return fail(tr("已有参考附件结构损坏，拒绝覆盖。"));
 if(existing.value()){if(existing.value()->encoded==sha256(encoded)){originalReferenceNotice_=tr("同一原彩已保存，没有重复导入。");emit changed();return true;}return fail(tr("本版每工程仅一份原彩；如刚导入错误可撤销，不会替换或覆盖。"));}
 auto command=attachOriginalReference(s,std::move(encoded),image.width(),image.height());if(!command||!app_.apply(command.value()))return fail(tr("保存原彩附件失败，画作未改变。"));
 originalReferenceNotice_=tr("已嵌入原始文件，请保存工程。分享 .draw 会同时分享这份原彩；PNG/ORA 画作不含它。");emit changed();return true;
}catch(const std::bad_alloc&){originalReferenceNotice_=tr("原彩导入内存不足，请核对附件状态；未缩小原图。");emit changed();return false;}
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
    if(s->spec.pixelSize.width>8192 || s->spec.pixelSize.height>8192 || s->spec.pixelSize.width*s->spec.pixelSize.height>20000000)return fail(tr("当前桌面填色上限为 2000 万像素、单边 8192；不会缩小图片。"));
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
bool QtBridge::fillLineartAt(double x,double y,const QColor& color,bool hollow,std::optional<QRectF> limit,std::vector<DocumentPoint> polygon,std::optional<QPointF> gapEnd) try {
    auto s=app_.snapshot();
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    if(!s || busy() || replaying() || workflow_->pending() || !activeLayer_ || !color.isValid() || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=double(s->spec.pixelSize.width) || y>=double(s->spec.pixelSize.height))return fail(tr("填色未执行：请选可编辑色层并结束当前操作。"));
    if(gapEnd){const double coords[]={x,y,gapEnd->x(),gapEnd->y()};for(auto v:coords)if(!std::isfinite(v)||v<0||std::floor(v)!=v)return fail(tr("补线端点须为有效原图整像素；未修改工程。"));if(hollow||limit||!polygon.empty()||gapEnd->x()>=s->spec.pixelSize.width||gapEnd->y()>=s->spec.pixelSize.height)return fail(tr("补线端点越界或工具冲突；未修改工程。"));}
    if(!polygon.empty()&&(!hollow||limit))return fail(tr("套索与矩形限制不能同时应用；工程未修改。"));
    std::optional<Rect> bounds;
    if(limit){
        const auto r=*limit;const double values[]={r.x(),r.y(),r.width(),r.height()};
        for(const auto v:values)if(!std::isfinite(v)||v<0||std::floor(v)!=v)return fail(tr("矩形范围须为非负整数原图像素；未执行挖空。"));
        if(!hollow||r.width()<1||r.height()<1||r.right()>double(s->spec.pixelSize.width)||r.bottom()>double(s->spec.pixelSize.height)||x<r.left()||y<r.top()||x>=r.right()||y>=r.bottom())return fail(tr("矩形必须在画布内，并点击矩形内的墨块；未修改工程。"));
        bounds=Rect{std::int64_t(r.x()),std::int64_t(r.y()),std::uint64_t(r.width()),std::uint64_t(r.height())};
    }
    NodeId source;for(const auto&[id,node]:*s->nodes)if(node.stage==LineartStage){if(!source.empty())return fail(tr("目前只支持一个原线稿图层。"));source=id;}
    if(hollow && s->spec.background.transparent)return fail(tr("局部挖空是白纸覆盖；请新建时选择白纸。透明工程未修改。"));
    ColorValue paint;paint.activeChannels=3;paint.profileId=s->spec.colorSpec.profileId;paint.channels={color.redF(),color.greenF(),color.blueF(),0};paint.alpha=color.alphaF();
    fillPool_.setMaxThreadCount(1);fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();
    const auto snapshot=*s;auto target=*activeLayer_;NodeId hollowOverlay;bool derived=false;
    const auto& ordered=s->nodes->at(Document::rootId()).children;
    if((ordered.size()==3||ordered.size()==4) && ordered[1]==source){hollowOverlay=ordered[2];if(!hollow&&!gapEnd){derived=true;target=ordered.size()==4?ordered[3]:uuid();}}
    const auto event=uuid();const auto generation=canvasGeneration_;
    fillBusy_=true;fillNotice_=tr("正在按原尺寸线稿计算区域；尚未提交，可取消。请接受预览后再保存。");emit changed();
    fillPool_.start([this,snapshot,source,target,event,x,y,paint,stop,generation,hollow,derived,hollowOverlay,bounds,polygon=std::move(polygon),gapEnd]{
        Result<Command> result=error(ErrorCode::InvalidInput,"hollow.selection");
        std::uint64_t selectedInk=0;
        try{
            if(gapEnd)result=closeLineartGapCommand(snapshot,source,hollowOverlay.empty()?uuid():hollowOverlay,event,std::uint32_t(x),std::uint32_t(y),std::uint32_t(gapEnd->x()),std::uint32_t(gapEnd->y()),stop);
            else if(derived)result=derivedHollowFillCommand(snapshot,source,hollowOverlay,target,event,std::uint32_t(x),std::uint32_t(y),paint,stop);
            else if(!hollow)result=originalLineartFillCommand(snapshot,source,target,event,std::uint32_t(x),std::uint32_t(y),paint,stop);
            else{
                const auto w=snapshot.spec.pixelSize.width,h=snapshot.spec.pixelSize.height;auto raw=lineartRasterPixels(snapshot,source);
                if(!raw)result=raw.error();else if(w>8192||h>8192||w*h>20000000)result=error(ErrorCode::BudgetExceeded,"hollow.canvas-limit");else{
                    std::vector<Rgba8> pixels(std::size_t(w*h));bool cancelled=false;
                    for(std::size_t i=0;i<pixels.size();i++){if(i%16384==0&&stop.stop_requested()){cancelled=true;break;}const auto p=raw.value().subspan(i*4,4);const auto v=std::uint8_t(p[3]&&p[0]<255?0:255);pixels[i]={v,v,v,255};}
                    if(cancelled)result=error(ErrorCode::Cancelled,"hollow.cancelled");
                    else if(pixels[std::size_t(y)*w+std::size_t(x)].r!=0)result=error(ErrorCode::InvalidInput,"hollow.click-ink-not-paper");
                    else{auto selection=floodFill(pixels,std::uint32_t(w),std::uint32_t(h),std::uint32_t(x),std::uint32_t(y),0,{},stop);if(selection&&bounds)selection=clipFillSelection(selection.value(),std::uint32_t(w),std::uint32_t(h),*bounds,stop);if(selection&&!polygon.empty()){selection=clipPolygonSelection(selection.value(),std::uint32_t(w),std::uint32_t(h),polygon,stop);if(selection){bool inside=false;for(const auto& run:selection.value().runs)if(run.y==std::uint32_t(y)&&std::uint32_t(x)>=run.x&&std::uint32_t(x)<run.x+run.length){inside=true;break;}if(!inside)selection=error(ErrorCode::InvalidInput,"clip-polygon.seed-outside");}}if(!selection)result=selection.error();else{selectedInk=selection.value().pixelCount;std::vector<Rgba8>().swap(pixels);result=hollowInkOverlayCommand(snapshot,source,hollowOverlay.empty()?event:hollowOverlay,uuid(),selection.value(),1,stop);}}
                }
            }
        }catch(const std::bad_alloc&){result=error(ErrorCode::BudgetExceeded,"hollow.selection-allocation");}
        std::optional<DocumentSnapshot> preview;FillRegionReview review;
        try { if(result){
            FillMask mask;for(const auto& op:result.value().operations)if(const auto* paint=std::get_if<Paint>(&op))for(std::size_t i=0;i+1<paint->event.geometry.size();i+=2){const auto& a=paint->event.geometry[i];const auto& b=paint->event.geometry[i+1];const auto length=std::uint32_t(b.x-a.x);mask.runs.push_back({std::uint32_t(a.y),std::uint32_t(a.x),length});mask.pixelCount+=length;}
            auto inspected=reviewFillRegion(mask,std::uint32_t(snapshot.spec.pixelSize.width),std::uint32_t(snapshot.spec.pixelSize.height),stop);
            if(!inspected)result=inspected.error();else{
                review=inspected.value();auto candidate=Document::fromSnapshot(snapshot);
                if(!candidate)result=candidate.error();else{auto applied=candidate.value().apply(result.value(),snapshot.revision);if(!applied)result=applied.error();else preview=candidate.value().snapshot();}
            }
        }
        }catch(const std::bad_alloc&){preview.reset();result=error(ErrorCode::BudgetExceeded,"fill-review.allocation");}
        QMetaObject::invokeMethod(this,[this,snapshot,result=std::move(result),preview,review,stop,generation,hollow,selectedInk,derived,gapEnd]() mutable {
            const auto current=app_.snapshot();fillBusy_=false;
            if(stop.stop_requested()){fillNotice_=tr("区域操作已取消，工程未修改。");emit changed();return;}
            if(replaying() || !current || current->documentId!=snapshot.documentId || current->revision!=snapshot.revision || canvasGeneration_!=generation){fillNotice_=tr("原工程已变化，旧填色结果已丢弃。");emit changed();return;}
            if(!result){
                if(hollow && result.error().messageKey=="hollow.no-safe-interior")fillNotice_=tr("此处是细笔触或没有可安全挖空的内部，已保留，不会扩大选区。");
                else if(hollow && result.error().messageKey=="hollow.already-cleared")fillNotice_=tr("所选内部已经挖空，未重复覆盖；请点击另一个独立墨块。原稿和已填颜色未改。");
                else fillNotice_=tr("区域操作未执行，工程未修改：")+QString::fromStdString(result.error().messageKey);
                emit changed();return;
            }
            pendingFill_=std::move(result.value());pendingFillSource_=snapshot;pendingFillPreview_=preview;pendingFillGeneration_=generation;
            fillNotice_=tr("未提交预览：%1 像素；范围 %2×%3；%4。内部建议点 (%5,%6)，离区域外像素中心的曼哈顿距离 %7。%8请对照画面后接受或取消。")
                .arg(qulonglong(review.pixelCount)).arg(qlonglong(review.bounds.width)).arg(qlonglong(review.bounds.height))
                .arg(review.touchesCanvasEdge?tr("连到画布边缘，警惕背景漏选"):tr("未连画布边缘"))
                .arg(review.seedX).arg(review.seedY).arg(review.clearance).arg(review.tiny?tr("注意：极小色块，可能点中了碎岛。 "):QString{});
            if(derived){bool lower=false;for(const auto& op:pendingFill_->operations)if(const auto* paint=std::get_if<Paint>(&op))lower=paint->event.layerId==snapshot.nodes->at(Document::rootId()).children.front();
                fillNotice_=(lower?tr("原线稿白区填色预览：接受后写入下方色层；挖空区域和上方颜色不变。参考原图及已接受补线，不采已填颜色。"):tr("挖空内部填色预览：接受后写入上方色层；原线稿白区和下方颜色不变。仅取样原图加已接受覆盖。"))+"\n"+fillNotice_;}

            if(hollow)fillNotice_=tr("局部挖空未提交：选中墨块 %1 像素，仅清空安全内部 %2 像素。原轮廓、细笔触及其他区域保留；白纸覆盖，不是透明擦除。请对照预览后接受或取消。").arg(qulonglong(selectedInk)).arg(qulonglong(review.pixelCount));
            if(gapEnd)fillNotice_=tr("短断口补线未提交：%1 像素，黑色1像素连接。仅连接指定端点，不改原图；接受后填色才会参考此线。请确认没有封错位置。").arg(qulonglong(review.pixelCount));
            emit changed();
        },Qt::QueuedConnection);
    });
    return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("填色任务启动内存不足，未修改工程。");emit changed();return false;}
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
    if(busy()){fillNotice_=tr("请等待或取消当前任务后再打开工程。");emit changed();return;}
    if(!p.isLocalFile())return;
    auto done=app_.open(std::filesystem::path(p.toLocalFile().toStdWString()));
    if(done){replayCount_=-1;referenceUrl_=QUrl{};referenceFeedback_=tr("Reference previews are not stored in .draw projects. Reimport the source image to compare.");++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());}
    emit changed();
}
void QtBridge::saveUrl(const QUrl& p){if(strokeCommitBusy_){fillNotice_=tr("干墨笔迹仍在验收；请等待完成或取消后再保存。");emit changed();return;}if(fillBusy_ || fillPending()){fillNotice_=tr("填色尚未提交，本次未保存；请等待完成或取消后重试。");emit changed();return;}if(!p.isLocalFile())return;if(workflow_->pending()){workflow_->fail(tr("请先接受审阅蒙版，再保存工程。未接受的修改仍然保留；也可取消关闭或明确放弃。" ));return;}auto done=app_.save(std::filesystem::path(p.toLocalFile().toStdWString()));(void)done;emit changed();}
void QtBridge::probeWorker(){if(busy()||!app_.snapshot())return;auto s=*app_.snapshot();auto bytes=encodeDelta(DocumentSnapshot{},s);auto hash=stateHash(s);if(!bytes||!hash)return;JobRequest q;q.id=uuid();q.nonce=uuid();q.task="native.smoke";q.inputSnapshot={s.documentId,s.revision,hash.value()};q.snapshotBytes=std::move(bytes.value());q.resources={q.id,ResourceDevice::CPU,16*1024*1024,0,0,false};auto id=broker_.submit(q);if(id){running_=id.value();workerMessage_=tr("Connecting native worker...");}else workerMessage_=QString::fromStdString(id.error().messageKey);emit changed();}
void QtBridge::cancelWorker(){if(workflow_->busy())workflow_->cancel();if(running_){broker_.cancel(*running_);workerMessage_=tr("Cancelling worker...");emit changed();}}
}

