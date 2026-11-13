#include "CanvasItem.h"
#include "CanvasFrame.h"
#include "brush/PressureTaper.h"
#include "brush/DryInkStroke.h"
#include "brush/FreehandStroke.h"
#include "brush/ChiselInkStroke.h"
#include "brush/ContinuousEraser.h"
#include "brush/MixedLineartStack.h"
#include "brush/ClipPolygonSelection.h"
#ifdef DRAWBORAD_HAS_QT
#include "apps/studio/QtBridge.h"
#include "view/Viewport.h"
#include "view/LayerTree.h"
#include <QInputDevice>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QSGOpacityNode>
#include <QSGTransformNode>
#include <QSGClipNode>
#include <QMatrix4x4>
#include <QTabletEvent>
#include <QQuickWindow>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
namespace drawborad {
CanvasItem::CanvasItem(QQuickItem* parent) : QQuickItem(parent) {
    previewTimer_.setSingleShot(true);previewTimer_.setInterval(33);connect(&previewTimer_,&QTimer::timeout,this,&CanvasItem::requestPreview);
    dryCommitTimer_.setInterval(25);connect(&dryCommitTimer_,&QTimer::timeout,this,&CanvasItem::finishDryCommit);
    brushSettings_.width=12;brushSettings_.spacing=.2;
    renderPool_.setMaxThreadCount(1);
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    connect(this,&CanvasItem::viewChanged,this,&CanvasItem::footprintChanged);
    connect(this,&CanvasItem::brushSettingsChanged,this,&CanvasItem::footprintChanged);
    connect(this,&CanvasItem::renderErrorChanged,this,&CanvasItem::footprintChanged);
    setClip(true);
}
CanvasItem::~CanvasItem(){cancelStroke();renderStop_.request_stop();renderPool_.clear();renderPool_.waitForDone();}
bool CanvasItem::previewing() const {std::lock_guard lock(snapshotMutex_);return previewing_||previewInputPending_;}
bool CanvasItem::rendering() const {std::lock_guard lock(snapshotMutex_);return rendering_;}
namespace {
Result<StrokeEvent> compileDesktopGesture(const DocumentSnapshot& source,std::span<const InputSample> points,const BrushSettings& settings,bool eraser,bool dryInk,bool smoothInk,bool chiselInk,int chiselAngle,bool controlledEraser,Id id,std::stop_token stop={}) {
 if(eraser&&controlledEraser){auto command=continuousEraserCommand(source,settings.layerId,id,points,settings.width,settings.opacity,stop);if(!command)return command.error();return std::get<Paint>(command.value().operations.front()).event;}
 if(chiselInk&&!eraser){auto command=chiselInkStrokeCommand(source,settings.layerId,id,points,settings.width,settings.color,std::uint8_t(chiselAngle),settings.opacity,stop);if(!command)return command.error();return std::get<Paint>(command.value().operations.front()).event;}
 if(smoothInk&&!eraser){auto command=smoothInkStrokeCommand(source,settings.layerId,id,points,settings.width,settings.color,settings.opacity,stop);if(!command)return command.error();return std::get<Paint>(command.value().operations.front()).event;}
 if(dryInk&&!eraser){auto command=dryInkStrokeCommand(source,settings.layerId,id,points,settings.width,settings.color,settings.opacity,stop);if(!command)return command.error();return std::get<Paint>(command.value().operations.front()).event;}
 const auto layer=source.nodes->find(settings.layerId);const bool rootDetail=layer!=source.nodes->end()&&layer->second.stage==FinishingLayerStage&&layer->second.parent==Document::rootId()&&layer->second.objectId.empty()&&nativeLayerPaintable(source,settings.layerId);
 BrushEngine engine;auto event=engine.compile(points,settings,eraser,rootDetail);
 if(event){auto& e=event.value();e.id=id;if(rootDetail){std::uint64_t tick=0;for(const auto& old:*source.eventIndex){if(old.endUs==UINT64_MAX)return error(ErrorCode::Overflow,"mixed.timeline");tick=std::max(tick,old.endUs+1);}const auto start=e.startUs,duration=e.endUs-e.startUs;if(duration>UINT64_MAX-tick)return error(ErrorCode::Overflow,"mixed.timeline");for(auto& p:e.geometry)p.timestampUs=tick+(p.timestampUs-start);e.startUs=tick;e.endUs=tick+duration;}}
 return event;
}
}
QVariantList CanvasItem::brushFootprint() const {
 QVariantList out;if(myPaintTool_||!brushPointer_||brushPointer_->x()<0||brushPointer_->y()<0||brushPointer_->x()>=width()||brushPointer_->y()>=height()||!bridge_||!isEnabled()||!isVisible()||bridge_->busy()||bridge_->replaying()||rendering()||!renderError().isEmpty()||!stroke_.empty()||panGesture_||bucketTool_||hollowTool_||hollowDragTool_||closeGapTool_)return out;
 const auto snap=bridge_->canvasSnapshot();if(!snap||snap->spec.colorSpec.format.mode!=ColorMode::RGB||!paintTarget(*snap)||!itemToDocument(*brushPointer_,*snap))return out;
 const auto rect=documentRect();const double scale=rect.width()/double(snap->spec.pixelSize.width),a=brushSettings_.width*scale*.5,b=a/(chiselInk_?3:1),angle=chiselInk_?chiselAngle_*3.14159265358979323846/8:0;
 if(!std::isfinite(a)||a<=0)return out;
 const double co=std::cos(angle),si=std::sin(angle);out.reserve(64);for(int i=0;i<64;++i){const double t=i*2*3.14159265358979323846/64,x=a*std::cos(t),y=b*std::sin(t);out.push_back(QVariant::fromValue(QPointF(brushPointer_->x()+co*x-si*y,brushPointer_->y()+si*x+co*y)));}return out;
}
void CanvasItem::hoverEnterEvent(QHoverEvent*e){if(!e->pointingDevice()||(e->pointingDevice()->type()!=QInputDevice::DeviceType::Mouse&&e->pointingDevice()->type()!=QInputDevice::DeviceType::TouchPad))brushPointer_.reset();else brushPointer_=e->position();emit footprintChanged();e->accept();}
void CanvasItem::hoverMoveEvent(QHoverEvent*e){if(!e->pointingDevice()||(e->pointingDevice()->type()!=QInputDevice::DeviceType::Mouse&&e->pointingDevice()->type()!=QInputDevice::DeviceType::TouchPad))brushPointer_.reset();else brushPointer_=e->position();emit footprintChanged();e->accept();}
void CanvasItem::hoverLeaveEvent(QHoverEvent*e){brushPointer_.reset();emit footprintChanged();e->accept();}
bool CanvasItem::configureMyPaint(bool value,const QString& preset,double scale){
 if((value&&(!bridge_||!bridge_->myPaintAvailable()))||(bridge_&&bridge_->busy())||!std::isfinite(scale)||scale<.25||scale>8||(preset!="pointy_ink"&&preset!="dry_brush"))return false;
 cancelStroke();myPaintTool_=value;myPaintPreset_=preset;myPaintScale_=scale;
 if(value){dryInk_=false;smoothInk_=false;chiselInk_=false;controlledEraser_=false;bucketTool_=false;hollowTool_=false;hollowDragTool_=false;hollowLassoTool_=false;closeGapTool_=false;}
 brushNotice_=value?tr("MyPaint：拖动期间不显示替代墨迹，松笔后真实预览再接受。radius scale 是预设倍率，不是像素宽度；倾斜不支持。"):QString{};emit brushSettingsChanged();return true;
}
bool CanvasItem::configureControlledEraser(bool value){
 if(value&&brushSettings_.width>128){brushNotice_=tr("连续橡皮宽度上限128原像素。");emit brushSettingsChanged();return false;}
 if(gestureMyPaint_||value!=controlledEraser_){cancelStroke();}controlledEraser_=value;
 if(value){myPaintTool_=false;dryInk_=false;smoothInk_=false;chiselInk_=false;setBucketTool(false);setHollowTool(false);setHollowDragTool(false);setCloseGapTool(false);}
 brushNotice_=value?tr("只擦当前独立手绘层的透明度，原稿/其他层不动；已用于填色的边界会拒绝。格式22须另存新文件。"):QString{};emit brushSettingsChanged();return true;
}
bool CanvasItem::configureDryInk(bool value){
 if(value&&brushSettings_.width>128){brushNotice_=tr("干墨宽度上限128原像素，请先调小宽度。");emit brushSettingsChanged();return false;}
 if(gestureMyPaint_||value!=dryInk_||(value&&(smoothInk_||chiselInk_||controlledEraser_))){cancelStroke();}dryInk_=value;if(value){myPaintTool_=false;smoothInk_=false;chiselInk_=false;controlledEraser_=false;}brushNotice_.clear();emit brushSettingsChanged();return true;
}
bool CanvasItem::configureSmoothInk(bool value){
 if(value&&brushSettings_.width>128){brushNotice_=tr("连续实墨宽度上限128原像素，请先调小宽度。");emit brushSettingsChanged();return false;}
 if(gestureMyPaint_||value!=smoothInk_||(value&&(dryInk_||chiselInk_||controlledEraser_))){cancelStroke();}smoothInk_=value;if(value){myPaintTool_=false;dryInk_=false;chiselInk_=false;controlledEraser_=false;}brushNotice_.clear();emit brushSettingsChanged();return true;
}
bool CanvasItem::configureChiselInk(bool value,int angle){
 if(angle<0||angle>7||(value&&brushSettings_.width>128)){brushNotice_=tr("连续扁笔尖角度需0..7、宽度≤128原像素。");emit brushSettingsChanged();return false;}
 if(gestureMyPaint_||value!=chiselInk_||angle!=chiselAngle_||(value&&(dryInk_||smoothInk_||controlledEraser_)))cancelStroke();
 chiselInk_=value;chiselAngle_=angle;if(value){myPaintTool_=false;dryInk_=false;smoothInk_=false;controlledEraser_=false;}
 brushNotice_=value?tr("固定3:1椭圆笔尖，不跟随数位笔倾角；新笔迹使用格式21，请另存新文件名保留旧工程。"):QString{};emit brushSettingsChanged();return true;
}
bool CanvasItem::configureBrush(double width,QColor color,double opacity,double hardness,double taper){
    if(!std::isfinite(width)||width<.5||width>512||((dryInk_||smoothInk_||chiselInk_||controlledEraser_)&&width>128)||!color.isValid()||!std::isfinite(opacity)||opacity<.01||opacity>1||!std::isfinite(hardness)||hardness<0||hardness>1||!std::isfinite(taper)||taper<0||taper>.5)return false;
    brushSettings_.width=width;brushSettings_.opacity=opacity;brushSettings_.hardness=hardness;brushColor_=color;mouseTaper_=taper;brushNotice_.clear();emit brushSettingsChanged();return true;
}
QObject* CanvasItem::studio() const { return bridge_; }
void CanvasItem::setStudio(QObject* value) {
    auto next = qobject_cast<QtBridge*>(value);
    if (bridge_ == next) return;
    if (bridge_) disconnect(bridge_, nullptr, this, nullptr);
    cancelStroke();
    bridge_ = next;
    if (bridge_) {connect(bridge_, &QtBridge::changed, this, &CanvasItem::refreshSnapshot);connect(bridge_, &QtBridge::cubicControlsChanged, this, &CanvasItem::viewChanged);}
    refreshSnapshot();
    emit studioChanged();
}
QVariantList CanvasItem::cubicHandles() const {
    QVariantList out;if(!bridge_)return out;const auto snap=bridge_->canvasSnapshot();if(!snap)return out;
    double zoom;QPointF pan;{std::lock_guard lock(snapshotMutex_);zoom=zoom_;pan=pan_;}
    const auto transform=documentTransform(*snap,zoom,pan);
    for(const auto& value:bridge_->cubicControls()){const auto p=value.toMap();const auto mapped=transform.map(QPointF(p["x"].toDouble(),p["y"].toDouble()));out.push_back(QVariantMap{{"x",mapped.x()},{"y",mapped.y()}});}return out;
}
bool CanvasItem::moveCubicHandle(int index,double x,double y){
    if(!bridge_||!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=width()||y>=height())return false;
    const auto snap=bridge_->canvasSnapshot();if(!snap)return false;double zoom;QPointF pan;{std::lock_guard lock(snapshotMutex_);zoom=zoom_;pan=pan_;}
    bool valid=false;const auto inverse=documentTransform(*snap,zoom,pan).inverted(&valid);if(!valid)return false;const auto p=inverse.map(QPointF(x,y));
    return bridge_->moveCubicControl(index,p.x(),p.y());
}
void CanvasItem::refreshSnapshot() {
    const auto next = bridge_ ? bridge_->displaySnapshot() : std::nullopt;
    bool changedDocument=false;{std::lock_guard lock(snapshotMutex_);changedDocument=(!snapshot_&&!next)?false:(!snapshot_||!next||snapshot_->documentId!=next->documentId);}
    if(changedDocument){brushPointer_.reset();emit footprintChanged();}
    const auto nextCount=bridge_&&!bridge_->fillPending()?std::uint64_t(bridge_->replayCount()):UINT64_MAX;
    if(bridge_&&(bridge_->replaying()||(bridge_->busy()&&!(dryCommitPending_&&bridge_->strokeCommitBusy()))))cancelStroke();
    if (strokeTarget_ && (!bridge_ || !next || strokeTarget_->document != next->documentId ||
                          strokeTarget_->generation != bridge_->canvasGeneration())) {
        cancelStroke();
    }
    bool unchanged=false;
    {
        std::lock_guard lock(snapshotMutex_);
        unchanged=snapshot_ && next && snapshot_->documentId == next->documentId &&
            snapshot_->revision == next->revision && snapshot_->nodes == next->nodes &&
            snapshot_->eventIndex == next->eventIndex && replayCount_==nextCount;
    }
    if(unchanged){emit viewChanged();return;}
    if(dryCommitPending_)cancelStroke();
    cancelHollowDrag();
    // A changed document/replay invalidates an in-progress input gesture.
    previewTimer_.stop();stroke_.clear();strokeTarget_.reset();tabletStroke_=false;gestureMyPaint_=false;
    renderStop_.request_stop();renderPool_.clear();renderStop_=std::stop_source{};
    const auto token=renderStop_.get_token();std::uint64_t generation;
    {
        std::lock_guard lock(snapshotMutex_);
        if(!snapshot_ || !next || snapshot_->documentId!=next->documentId || snapshot_->spec.pixelSize!=next->spec.pixelSize){zoom_=1;pan_={};panGesture_.reset();}
        generation=++renderGeneration_;completedFrame_={};completedError_={};
        snapshot_=next;replayCount_=nextCount;previewing_=false;previewInputPending_=false;committedFrame_={};displayFrame_={};renderError_={};legacyDisplay_=false;rendering_=bool(next);
    }
    emit renderErrorChanged();update();emit viewChanged();
    if(!next)return;
    renderPool_.start([this,next,nextCount,token,generation]{
        if(token.stop_requested())return;
        QImage frame;QString failure;bool legacy=false;
        auto rendered=renderCanvasFrame(*next,nextCount,{},token);
        if(token.stop_requested())return;
        if(rendered){frame=rendered.value().convertToFormat(QImage::Format_RGBA8888_Premultiplied);if(frame.isNull())failure=QStringLiteral("canvas.image-allocation");}
        else {
            legacy=rendered.error().messageKey=="merged.native-compositing-required" &&
                std::all_of(next->nodes->begin(),next->nodes->end(),[&](const auto& entry){return entry.second.kind!=NodeKind::Layer || !layerVisible(*next,entry.first) || nativeLayerSupported(*next,entry.first);});
            if(!legacy)failure=QString::fromStdString(rendered.error().messageKey);
        }
        if(token.stop_requested())return;
        // One completion mailbox: stale queued notifications never retain poster images.
        {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_)return;completedFrame_=std::move(frame);completedError_=failure;completedLegacy_=legacy;}
        QMetaObject::invokeMethod(this,[this,generation]{
            {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_)return;displayFrame_=std::move(completedFrame_);committedFrame_=displayFrame_;renderError_=completedError_;legacyDisplay_=completedLegacy_;rendering_=false;}
            emit renderErrorChanged();update();
        },Qt::QueuedConnection);
    });
}

std::optional<std::pair<NodeId, NodeId>> CanvasItem::paintTarget(const DocumentSnapshot& s) const {
    if (bridge_)
        if (const auto id = bridge_->activeLayer(); id && nativeLayerPaintable(s, *id))
            return std::pair{s.nodes->at(*id).objectId, *id};
    return std::nullopt;
}
QTransform CanvasItem::documentTransform(const DocumentSnapshot& snapshot,double magnification,QPointF offset) const {
    const auto size=snapshot.spec.pixelSize;
    const double scale=std::min(width()/double(size.width),height()/double(size.height))*magnification;
    QTransform transform;
    transform.translate((width()-double(size.width)*scale)*.5+offset.x(),
                        (height()-double(size.height)*scale)*.5+offset.y());
    transform.scale(scale,scale);return transform;
}
QPointF CanvasItem::boundedPan(const DocumentSnapshot& snapshot,double magnification,QPointF offset) const {
    const auto rect=documentTransform(snapshot,magnification,{}).mapRect(QRectF(0,0,double(snapshot.spec.pixelSize.width),double(snapshot.spec.pixelSize.height)));
    // Leave part of the document reachable even after repeated pan gestures.
    const auto maxX=std::max(0.0,(rect.width()-width())*.5)+width()*.4;
    const auto maxY=std::max(0.0,(rect.height()-height())*.5)+height()*.4;
    return {std::clamp(offset.x(),-maxX,maxX),std::clamp(offset.y(),-maxY,maxY)};
}
QString CanvasItem::renderError() const {std::lock_guard lock(snapshotMutex_);return renderError_;}
double CanvasItem::zoom() const {std::lock_guard lock(snapshotMutex_);return zoom_;}
QVariantList CanvasItem::localStrokeHandles() const {
    QVariantList out;const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;
    if(!snap)return out;
    const auto points=bridge_->localStrokePoints();
    const auto r=documentRect();const auto w=snap->spec.pixelSize.width,h=snap->spec.pixelSize.height;
    if(!w||!h)return out;
    for(const auto& value:points){const auto p=value.toMap();out.push_back(QVariantMap{{"x",r.x()+p["x"].toDouble()*r.width()/w},{"y",r.y()+p["y"].toDouble()*r.height()/h},{"scale",r.width()/w},{"pressure",p["pressure"]}});}
    return out;
}
QRectF CanvasItem::documentRect() const {
    std::lock_guard lock(snapshotMutex_);if(!snapshot_)return {};
    return documentTransform(*snapshot_,zoom_,pan_).mapRect(QRectF(0,0,double(snapshot_->spec.pixelSize.width),double(snapshot_->spec.pixelSize.height)));
}
bool CanvasItem::zoomAt(double factor,double x,double y){
    if(!std::isfinite(factor) || factor<=0 || !std::isfinite(x) || !std::isfinite(y))return false;
    {
        std::lock_guard lock(snapshotMutex_);if(!snapshot_ || width()<=0 || height()<=0)return false;
        const auto next=std::clamp(zoom_*factor,.25,16.0);
        const QPointF anchor{x-width()*.5,y-height()*.5};
        pan_=boundedPan(*snapshot_,next,anchor-(anchor-pan_)*(next/zoom_));zoom_=next;
    }
    cancelStroke();
    update();emit viewChanged();return true;
}
bool CanvasItem::panBy(double x,double y){
    if(!std::isfinite(x) || !std::isfinite(y))return false;
    {
        std::lock_guard lock(snapshotMutex_);if(!snapshot_)return false;
        pan_=boundedPan(*snapshot_,zoom_,pan_+QPointF{x,y});
    }
    cancelStroke();
    update();emit viewChanged();return true;
}
void CanvasItem::fitDocument(){
    {std::lock_guard lock(snapshotMutex_);zoom_=1;pan_={};}
    cancelStroke();panGesture_.reset();
    update();emit viewChanged();
}
void CanvasItem::geometryChange(const QRectF& next,const QRectF& previous){
    if(next!=previous){cancelHollowDrag();brushPointer_.reset();emit footprintChanged();}
    QQuickItem::geometryChange(next,previous);
    {std::lock_guard lock(snapshotMutex_);if(snapshot_)pan_=boundedPan(*snapshot_,zoom_,pan_);}
    update();emit viewChanged();
}
std::optional<DocumentPoint> CanvasItem::itemToDocument(const QPointF& p,const DocumentSnapshot& s) const {
    if(width()<=0 || height()<=0)return std::nullopt;
    double viewZoom;QPointF panOffset;{std::lock_guard lock(snapshotMutex_);viewZoom=zoom_;panOffset=pan_;}
    bool invertible=false;const auto inverse=documentTransform(s,viewZoom,panOffset).inverted(&invertible);
    if(!invertible)return std::nullopt;
    const auto point=inverse.map(p);
    DocumentPoint docPoint{point.x(),point.y()};
    if(!std::isfinite(docPoint.x)||!std::isfinite(docPoint.y)||docPoint.x<0||docPoint.y<0||docPoint.x>=double(s.spec.pixelSize.width)||docPoint.y>=double(s.spec.pixelSize.height))return std::nullopt;
    return docPoint;
}
InputSample CanvasItem::mouseSample(QMouseEvent* e) const {
    InputSample s; std::optional<DocumentSnapshot> snap; {std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(snap)if(auto p=itemToDocument(e->position(),*snap)){s.x=p->x;s.y=p->y;}
    s.pressure=1.0;s.timestampUs=std::uint64_t(e->timestamp())*1000;s.deviceKind=std::uint32_t(QInputDevice::DeviceType::Mouse);s.capabilities=0;return s;
}
InputSample CanvasItem::tabletSample(QTabletEvent* e) const {
    TabletPacket p;std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(snap)if(auto docPoint=itemToDocument(e->position(),*snap)){p.x=docPoint->x;p.y=docPoint->y;}
    p.timestampUs=std::uint64_t(e->timestamp())*1000;p.deviceKind=std::uint32_t(e->device()->type());
    const auto dev=e->device();
    if(dev&&dev->hasCapability(QInputDevice::Capability::Pressure)){p.capabilities|=PressureCap;p.pressure=e->pressure();}
    if(dev&&(dev->hasCapability(QInputDevice::Capability::XTilt)||dev->hasCapability(QInputDevice::Capability::YTilt))){p.capabilities|=TiltCap;p.tiltX=e->xTilt();p.tiltY=e->yTilt();}
    if(e->pointingDevice()&&e->pointingDevice()->pointerType()==QPointingDevice::PointerType::Eraser){p.capabilities|=EraserCap;p.eraser=true;}
    return normalizeTabletSample(p);
}
void CanvasItem::beginSample(InputSample sample, bool tablet, bool eraser) {
    cancelStroke();
    tabletStroke_ = tablet;
    eraser_ = eraser||controlledEraser_;
    if (!bridge_ || bridge_->busy() || bridge_->replaying() || rendering() || !renderError().isEmpty()) return;
    const auto snapshot = bridge_->canvasSnapshot();
    if (!snapshot) return;
    if(snapshot->spec.colorSpec.format.mode!=ColorMode::RGB){brushNotice_=QStringLiteral("RGB brush controls cannot paint a CMYK document without color conversion.");emit brushSettingsChanged();return;}
    if(myPaintTool_) {
        if(!bridge_->myPaintAvailable()||eraser_){brushNotice_=tr("MyPaint 后端不可用或当前是橡皮输入，本笔未提交。");emit brushSettingsChanged();return;}
        gestureMyPaint_=true;gestureMyPaintPreset_=myPaintPreset_;gestureMyPaintScale_=myPaintScale_;
        gestureColor_=brushColor_;gestureBrush_=brushSettings_;
        strokeTarget_=StrokeTarget{snapshot->documentId,{}, {},bridge_->canvasGeneration()};
        stroke_.push_back(sample);setFocus(true,Qt::MouseFocusReason);
        brushNotice_=tr("正在记录原始坐标/压力/时间；没有同引擎实时墨迹，松笔后计算真实预览。鼠标压力模拟、倾斜忽略。");emit brushSettingsChanged();emit renderErrorChanged();return;
    }
    gestureMyPaint_=false;
    const auto target = paintTarget(*snapshot);
    if (!target){if(controlledEraser_){brushNotice_=tr("橡皮没有可用的当前手绘层，未改动工程。");emit brushSettingsChanged();}return;}
    if(controlledEraser_&&bridge_->isSelectedBoundary(target->second)){brushNotice_=tr("当前层已选作填色边界，请先取消边界选择；原稿未改。");emit brushSettingsChanged();return;}
    strokeTarget_ = StrokeTarget{snapshot->documentId, target->first, target->second, bridge_->canvasGeneration()};
    brushNotice_.clear();emit brushSettingsChanged();
    renderStop_=std::stop_source{};
    gestureBrush_=brushSettings_;gestureBrush_.seed=sample.timestampUs;gestureColor_=brushColor_;gestureTaper_=mouseTaper_;gestureDryInk_=dryInk_;gestureSmoothInk_=smoothInk_;gestureChiselInk_=chiselInk_;gestureChiselAngle_=chiselAngle_;gestureControlledEraser_=controlledEraser_;
    const auto gestureBytes=QUuid::createUuid().toRfc4122();std::copy_n(reinterpret_cast<const std::uint8_t*>(gestureBytes.constData()),16,gestureId_.bytes.begin());
    stroke_.push_back(sample);{std::lock_guard lock(snapshotMutex_);++previewInputSequence_;previewInputPending_=true;}setFocus(true,Qt::MouseFocusReason);previewTimer_.start();emit renderErrorChanged();
}
void CanvasItem::appendSample(InputSample s){
 if(stroke_.empty())return;
 if(gestureMyPaint_){
  if(stroke_.size()>=1024||s.timestampUs<stroke_.back().timestampUs||s.timestampUs-stroke_.front().timestampUs>60000000){cancelStroke();brushNotice_=tr("MyPaint 单笔超过1024点/60秒或时间倒退，整笔取消，不截点。");emit brushSettingsChanged();return;}
  stroke_.push_back(s);emit renderErrorChanged();return;
 }
 if((((gestureDryInk_||gestureSmoothInk_||gestureChiselInk_)&&!eraser_)||gestureControlledEraser_)&&stroke_.size()>=4096){cancelStroke();brushNotice_=tr("笔迹超过4096输入点，整笔取消，未截点或改动原稿。");emit brushSettingsChanged();return;}
 stroke_.push_back(s);
 {std::lock_guard lock(snapshotMutex_);++previewInputSequence_;previewInputPending_=true;}
 if(!previewTimer_.isActive())previewTimer_.start();
 emit renderErrorChanged();
}
BrushSettings CanvasItem::currentGestureSettings(const DocumentSnapshot& snapshot)const{
    BrushSettings b=gestureBrush_;b.objectId=strokeTarget_->object;b.layerId=strokeTarget_->layer;b.parentRevision=snapshot.revision;
    b.color.profileId=snapshot.spec.colorSpec.profileId;b.color.activeChannels=3;b.color.channels={gestureColor_.redF(),gestureColor_.greenF(),gestureColor_.blueF(),0};b.color.alpha=gestureColor_.alphaF();return b;
}
void CanvasItem::clearPreview(){
    previewTimer_.stop();renderStop_.request_stop();renderPool_.clear();
    {std::lock_guard lock(snapshotMutex_);++renderGeneration_;completedDryReady_=false;completedDryEvent_.reset();completedDryError_.clear();completedFrame_={};displayFrame_=committedFrame_;previewing_=false;previewInputPending_=false;}
    emit renderErrorChanged();update();
}
void CanvasItem::cancelStroke(){cancelHollowDrag();if(stroke_.empty()&&!previewing()&&!previewTimer_.isActive()&&!dryCommitPending_)return;const bool wasPending=dryCommitPending_;dryCommitPending_=false;dryCommitTimer_.stop();{std::lock_guard lock(snapshotMutex_);completedDryReady_=false;completedDryEvent_.reset();completedDryError_.clear();}stroke_.clear();strokeTarget_.reset();tabletStroke_=false;gestureMyPaint_=false;clearPreview();if(wasPending){if(bridge_)bridge_->setStrokeCommitBusy(false);brushNotice_=tr("已取消笔迹验收，这笔未写入工程。");emit brushSettingsChanged();}}
void CanvasItem::mouseUngrabEvent(){if(!dryCommitPending_)cancelStroke();}
void CanvasItem::requestPreview() try{
    if(gestureMyPaint_)return;
    if(!bridge_||bridge_->busy()||bridge_->replaying()||stroke_.empty()||!strokeTarget_||rendering()){clearPreview();renderStop_=std::stop_source{};return;}
    const auto source=bridge_->canvasSnapshot();if(!source){clearPreview();renderStop_=std::stop_source{};return;}
    const auto settings=currentGestureSettings(*source);const auto samples=stroke_;const auto taper=tabletStroke_?0:gestureTaper_;const auto eraser=eraser_;const auto dryInk=gestureDryInk_;const auto smoothInk=gestureSmoothInk_;const auto chiselInk=gestureChiselInk_;const auto chiselAngle=gestureChiselAngle_;const auto controlledEraser=gestureControlledEraser_;const auto gestureId=gestureId_;
    // Do not starve a running preview during continuous input. Replace only queued
    // work, then display completed snapshots in order until the newest catches up.
    renderPool_.clear();const auto token=renderStop_.get_token();std::uint64_t generation,sequence,inputSequence;
    {std::lock_guard lock(snapshotMutex_);generation=renderGeneration_;sequence=++previewRequestSequence_;inputSequence=previewInputSequence_;previewing_=true;}
    emit renderErrorChanged();
    renderPool_.start([this,source,settings,samples,taper,eraser,dryInk,smoothInk,chiselInk,chiselAngle,controlledEraser,gestureId,token,generation,sequence,inputSequence]{
        if(token.stop_requested())return;
        QImage frame;QString failure;
        try{
        auto input=samples;if(taper>0){auto tapered=taperMousePressure(input,taper,true);if(tapered)input=std::move(tapered.value());}
        auto event=compileDesktopGesture(*source,input,settings,eraser,dryInk,smoothInk,chiselInk,chiselAngle,controlledEraser,gestureId,token);
        if(event){
            auto document=Document::fromSnapshot(*source);auto bounds=strokeBounds(event.value());
            if(document&&bounds){auto applied=document.value().apply(Command{Paint{event.value(),bounds.value()}},source->revision);
                if(applied&&!token.stop_requested()){auto rendered=renderCanvasFrame(document.value().snapshot(),UINT64_MAX,{},token);if(rendered)frame=rendered.value().convertToFormat(QImage::Format_RGBA8888_Premultiplied);else failure=QString::fromStdString(rendered.error().messageKey);}
            }
        }else failure=QString::fromStdString(event.error().messageKey);
        }catch(const std::bad_alloc&){failure=QStringLiteral("preview.allocation");}
        if(token.stop_requested())return;
        if(frame.isNull()&&failure.isEmpty())failure=QStringLiteral("Preview unavailable; committed document unchanged.");
        {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_)return;completedFrame_=std::move(frame);completedError_=failure;completedPreviewSequence_=sequence;}
        QMetaObject::invokeMethod(this,[this,generation,sequence,inputSequence]{
            {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_||sequence!=completedPreviewSequence_)return;if(!completedFrame_.isNull())displayFrame_=std::move(completedFrame_);previewing_=sequence<previewRequestSequence_;if(inputSequence==previewInputSequence_)previewInputPending_=false;brushNotice_=completedError_;}
            emit renderErrorChanged();emit brushSettingsChanged();update();
        },Qt::QueuedConnection);
    });
}catch(const std::bad_alloc&){clearPreview();renderStop_=std::stop_source{};brushNotice_=QStringLiteral("Preview allocation failed; stroke input retained until release.");emit brushSettingsChanged();}
void CanvasItem::finishStroke(){
    if(!bridge_||bridge_->busy()||bridge_->replaying()||stroke_.empty()||rendering()||!renderError().isEmpty()){cancelStroke();return;}std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(gestureMyPaint_){
        if(!snap||!strokeTarget_||strokeTarget_->document!=snap->documentId||strokeTarget_->generation!=bridge_->canvasGeneration()){cancelStroke();return;}
        auto samples=std::move(stroke_);const auto color=gestureColor_;const auto opacity=gestureBrush_.opacity;
        const auto preset=gestureMyPaintPreset_;const auto scale=gestureMyPaintScale_;const bool mouse=!tabletStroke_;
        stroke_.clear();strokeTarget_.reset();gestureMyPaint_=false;tabletStroke_=false;clearPreview();emit renderErrorChanged();
        const bool started=bridge_->previewMyPaintGesture(std::move(samples),color,opacity,preset,scale,mouse);
        brushNotice_=started?tr("已收笔，MyPaint 正在真实验收；未接受前原稿不变。"):tr("MyPaint 未启动，工程未改。");emit brushSettingsChanged();return;
    }
    if(!snap||!strokeTarget_||strokeTarget_->document!=snap->documentId||
       strokeTarget_->generation!=bridge_->canvasGeneration()||
       !nativeLayerPaintable(*snap,strokeTarget_->layer)||
       snap->nodes->at(strokeTarget_->layer).objectId!=strokeTarget_->object){cancelStroke();return;}
    BrushSettings b=currentGestureSettings(*snap);
    if(gestureControlledEraser_&&bridge_->isSelectedBoundary(b.layerId)){cancelStroke();brushNotice_=tr("当前层已选作填色边界，本次橡皮取消，工程未改。");emit brushSettingsChanged();return;}
    if(!tabletStroke_&&gestureTaper_>0){auto tapered=taperMousePressure(stroke_,gestureTaper_,true);if(tapered)stroke_=std::move(tapered.value());else {if((((gestureDryInk_||gestureSmoothInk_||gestureChiselInk_)&&!eraser_)||gestureControlledEraser_)){cancelStroke();brushNotice_=tr("笔迹收笔处理超限，原稿未变。");emit brushSettingsChanged();return;}brushNotice_=QStringLiteral("Stroke retained without mouse taper: input exceeds taper limits.");emit brushSettingsChanged();}}
    clearPreview();
    auto event=compileDesktopGesture(*snap,stroke_,b,eraser_,gestureDryInk_,gestureSmoothInk_,gestureChiselInk_,gestureChiselAngle_,gestureControlledEraser_,gestureId_);
    stroke_.clear();
    strokeTarget_.reset();
    emit renderErrorChanged();
    if(event&&(event.value().engineVersion==12||event.value().engineVersion==19||event.value().engineVersion==21||event.value().engineVersion==22||(event.value().objectId.empty()&&snap->nodes->at(event.value().layerId).stage==FinishingLayerStage))){commitDryStrokeAsync(*snap,std::move(event.value()));return;}
    if (event) {
        auto committed=bridge_->commitStroke(event.value());
        if(!committed){brushNotice_=tr("笔迹未提交，工程未修改：")+QString::fromStdString(committed.error().messageKey);emit brushSettingsChanged();}
    }else{brushNotice_=tr("笔迹未提交，工程未修改：")+QString::fromStdString(event.error().messageKey);emit brushSettingsChanged();}
}
void CanvasItem::commitDryStrokeAsync(DocumentSnapshot source,StrokeEvent event) try {
    if(!bridge_)return;
    renderStop_=std::stop_source{};const auto token=renderStop_.get_token();std::uint64_t generation;
    {std::lock_guard lock(snapshotMutex_);generation=renderGeneration_;previewing_=true;}
    dryCommitPending_=true;dryCommitDocument_=source.documentId;dryCommitRevision_=source.revision;dryCommitGeneration_=bridge_->canvasGeneration();bridge_->setStrokeCommitBusy(true);dryCommitTimer_.start();
    brushNotice_=tr("正在验收笔迹；通过后才写入工程。Esc 可取消。");emit brushSettingsChanged();emit renderErrorChanged();
    renderPool_.start([this,source=std::move(source),event=std::move(event),token,generation]() mutable {
        QString failure;
        try {
            auto document=Document::fromSnapshot(source);auto bounds=strokeBounds(event);
            if(!document)failure=QString::fromStdString(document.error().messageKey);
            else if(!bounds)failure=QString::fromStdString(bounds.error().messageKey);
            else {auto applied=document.value().apply(Command{Paint{event,bounds.value()}},source.revision);
                if(!applied)failure=QString::fromStdString(applied.error().messageKey);
                else {
                    const auto candidate=document.value().snapshot();
                    // A renderable gesture must not poison the controlled stack
                    // or exceed its aggregate event/point budget for later fills.
                    if(candidate.nodes->at(event.layerId).stage==FinishingLayerStage){
                        auto controlled=projectControlledLineart(candidate,token);
                        if(!controlled)failure=QString::fromStdString(controlled.error().messageKey);
                    }
                    if(failure.isEmpty()){auto frame=renderCanvasFrame(candidate,UINT64_MAX,{},token);if(!frame)failure=QString::fromStdString(frame.error().messageKey);}
                }
            }
        }catch(const std::bad_alloc&){failure=QStringLiteral("dry-ink.commit-allocation");}
        if(token.stop_requested())return;
        // Move into a bounded mailbox. No allocation/callback posting outside
        // the worker exception boundary; an existing GUI timer drains this.
        {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_)return;completedDryEvent_=std::move(event);completedDryError_=std::move(failure);completedDryReady_=true;}

    });
}catch(const std::bad_alloc&){cancelStroke();brushNotice_=tr("笔迹验收内存不足，原稿未变。");emit brushSettingsChanged();}
void CanvasItem::finishDryCommit() try {
    std::optional<StrokeEvent> event;QString failure;
    {std::lock_guard lock(snapshotMutex_);if(!completedDryReady_||!dryCommitPending_)return;event=std::move(completedDryEvent_);failure=std::move(completedDryError_);completedDryReady_=false;previewing_=false;}
    dryCommitTimer_.stop();dryCommitPending_=false;if(!bridge_)return;
    bridge_->setStrokeCommitBusy(false);const auto actual=bridge_->canvasSnapshot();
    if(!actual||actual->documentId!=dryCommitDocument_||actual->revision!=dryCommitRevision_||bridge_->canvasGeneration()!=dryCommitGeneration_){brushNotice_=tr("工程已改变，未提交旧笔迹。");}
    else if(!failure.isEmpty()){brushNotice_=tr("笔迹验收失败，原稿未变：")+failure;}
    else if(event){auto done=bridge_->commitStroke(*event);brushNotice_=done?QString{}:tr("笔迹未提交：")+QString::fromStdString(done.error().messageKey);}
    emit renderErrorChanged();emit brushSettingsChanged();update();
}catch(const std::bad_alloc&){dryCommitTimer_.stop();dryCommitPending_=false;if(bridge_)bridge_->setStrokeCommitBusy(false);brushNotice_=tr("笔迹提交内存不足，请核对工程状态。");emit brushSettingsChanged();}
void CanvasItem::setCloseGapTool(bool value){cancelStroke();closeGapTool_=value;if(value){myPaintTool_=false;controlledEraser_=false;hollowTool_=false;bucketTool_=false;hollowDragTool_=false;hollowLassoTool_=false;}emit brushSettingsChanged();emit viewChanged();}
QVariantList CanvasItem::closeGapGuide() const {QVariantList out;if(!closeGapTool_||!hollowDrag_)return out;const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!snap)return out;const auto r=documentRect();for(const auto p:{hollowDrag_->start,hollowDrag_->current})out.push_back(QVariant::fromValue(QPointF(r.x()+(std::floor(p.x())+.5)*r.width()/snap->spec.pixelSize.width,r.y()+(std::floor(p.y())+.5)*r.height()/snap->spec.pixelSize.height)));return out;}
void CanvasItem::setHollowDragTool(bool value){
 cancelStroke();closeGapTool_=false;hollowDragTool_=value;hollowLassoTool_=false;
 if(value){myPaintTool_=false;controlledEraser_=false;hollowTool_=true;bucketTool_=false;}
 emit brushSettingsChanged();emit viewChanged();
}
void CanvasItem::setHollowLassoTool(bool value){
 cancelStroke();closeGapTool_=false;hollowDragTool_=value;hollowLassoTool_=value;
 if(value){myPaintTool_=false;controlledEraser_=false;hollowTool_=true;bucketTool_=false;}
 emit brushSettingsChanged();emit viewChanged();
}
QVariantList CanvasItem::hollowLassoPoints() const {
 QVariantList out;if(hollowDrag_&&!hollowLassoTool_)return out;const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!snap)return out;
 const auto& points=hollowDrag_&&hollowLassoTool_?hollowDrag_->points:hollowPolygon_;
 if(!hollowBounded_&&!hollowDrag_)return out;
 const auto r=documentRect();for(const auto& p:points)out.push_back(QVariant::fromValue(QPointF(r.x()+p.x*r.width()/snap->spec.pixelSize.width,r.y()+p.y*r.height()/snap->spec.pixelSize.height)));return out;
}
void CanvasItem::cancelHollowDrag(){
 if(!hollowDrag_)return;
 hollowDrag_.reset();emit brushSettingsChanged();emit viewChanged();
}
QRectF CanvasItem::hollowDragRect() const {
 if(!hollowDrag_)return {};
 const auto a=hollowDrag_->start,b=hollowDrag_->current;
 if(a.x()==b.x()||a.y()==b.y())return {};
 const auto left=std::floor(std::min(a.x(),b.x())),top=std::floor(std::min(a.y(),b.y()));
 return {left,top,std::ceil(std::max(a.x(),b.x()))-left,std::ceil(std::max(a.y(),b.y()))-top};
}
bool CanvasItem::updateHollowDrag(QPointF point){
 if(!hollowDrag_)return false;
 const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;
 if(!snap||bridge_->busy()||bridge_->replaying()||rendering()||!renderError().isEmpty()||
    snap->documentId!=hollowDrag_->document||snap->revision!=hollowDrag_->revision||bridge_->canvasGeneration()!=hollowDrag_->generation){cancelHollowDrag();return false;}
 const auto p=itemToDocument(point,*snap);
 if(!p){cancelHollowDrag();return false;}
 hollowDrag_->current={p->x,p->y};
 if(hollowLassoTool_){auto& points=hollowDrag_->points;if(points.empty()||std::hypot(points.back().x-p->x,points.back().y-p->y)>=1){if(points.size()>=512){brushNotice_=tr("套索超过512点，已取消本次选择；旧范围和工程未改。");cancelHollowDrag();return false;}points.push_back(*p);}}
 emit viewChanged();return true;
}
QRectF CanvasItem::hollowItemRect() const {
 const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!snap)return {};
 const auto r=hollowDrag_?hollowDragRect():hollowBounds_;const double values[]={r.x(),r.y(),r.width(),r.height()};for(auto v:values)if(!std::isfinite(v)||v<0||std::floor(v)!=v)return {};
 const auto w=double(snap->spec.pixelSize.width),h=double(snap->spec.pixelSize.height);if(w<=0||h<=0||r.width()<1||r.height()<1||r.right()>w||r.bottom()>h)return {};
 const auto d=documentRect();return {d.x()+r.x()*d.width()/w,d.y()+r.y()*d.height()/h,r.width()*d.width()/w,r.height()*d.height()/h};
}
void CanvasItem::mousePressEvent(QMouseEvent* e){
    brushPointer_.reset();emit footprintChanged();
    if(e->button()==Qt::MiddleButton){cancelHollowDrag();panGesture_=e->position();emit footprintChanged();e->accept();return;}
    if(hollowDragTool_||closeGapTool_){
        cancelStroke();const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;
        if(e->button()==Qt::LeftButton&&snap&&!bridge_->busy()&&!bridge_->replaying()&&!rendering()&&renderError().isEmpty())
            if(const auto p=itemToDocument(e->position(),*snap)){
                forceActiveFocus();hollowDrag_=HollowDrag{{p->x,p->y},{p->x,p->y},snap->documentId,snap->revision,bridge_->canvasGeneration(),{{p->x,p->y}}};
                emit brushSettingsChanged();emit viewChanged();
            }
        e->accept();return;
    }
    if(bucketTool_ || hollowTool_){
        cancelStroke();const auto snap=bridge_?bridge_->canvasSnapshot():std::nullopt;
        if(e->button()==Qt::LeftButton && snap && !rendering() && renderError().isEmpty())if(auto point=itemToDocument(e->position(),*snap)){auto color=brushColor_;color.setAlphaF(color.alphaF()*brushOpacity());bridge_->fillLineartAt(point->x,point->y,color,hollowTool_,hollowTool_&&hollowBounded_&&hollowPolygon_.empty()?std::optional<QRectF>(hollowBounds_):std::nullopt,hollowTool_&&hollowBounded_?hollowPolygon_:std::vector<DocumentPoint>{});}
        e->accept();return;
    }
    const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(tabletStroke_||!s||!itemToDocument(e->position(),*s)){e->ignore();return;}beginSample(mouseSample(e),false,false);e->accept();
}
void CanvasItem::mouseMoveEvent(QMouseEvent* e){
    brushPointer_.reset();emit footprintChanged();
    if((hollowDragTool_||closeGapTool_)&&!panGesture_){
        if(!(e->buttons()&Qt::LeftButton))cancelHollowDrag();else updateHollowDrag(e->position());
        e->accept();return;
    }
    if(panGesture_){const auto delta=e->position()-*panGesture_;panGesture_=e->position();(void)panBy(delta.x(),delta.y());e->accept();return;}
    const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!tabletStroke_&&!stroke_.empty()&&s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));e->accept();
}
void CanvasItem::mouseReleaseEvent(QMouseEvent* e){
    brushPointer_.reset();emit footprintChanged();
    if(e->button()==Qt::MiddleButton){panGesture_.reset();emit footprintChanged();e->accept();return;}
    if(closeGapTool_){
        if(e->button()==Qt::LeftButton&&updateHollowDrag(e->position())){const auto start=hollowDrag_->start,end=hollowDrag_->current;cancelHollowDrag();bridge_->closeLineartGapAt(std::floor(start.x()),std::floor(start.y()),std::floor(end.x()),std::floor(end.y()));}e->accept();return;
    }
    if(hollowDragTool_){
        if(e->button()==Qt::LeftButton&&updateHollowDrag(e->position())){
            auto bounds=hollowDragRect();std::vector<DocumentPoint> polygon;
            if(hollowLassoTool_){
                polygon=hollowDrag_->points;if(polygon.size()>1&&std::hypot(polygon.front().x-polygon.back().x,polygon.front().y-polygon.back().y)<1)polygon.pop_back();
                const auto snap=bridge_->canvasSnapshot();auto valid=validateClipPolygon(polygon,std::uint32_t(snap->spec.pixelSize.width),std::uint32_t(snap->spec.pixelSize.height));
                if(!valid){brushNotice_=tr("套索无效（自交、退化或越界）；旧范围和工程未改：")+QString::fromStdString(valid.error().messageKey);cancelHollowDrag();e->accept();return;}
                double left=polygon.front().x,right=left,top=polygon.front().y,bottom=top;for(const auto& p:polygon){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}bounds={std::floor(left),std::floor(top),std::ceil(right)-std::floor(left),std::ceil(bottom)-std::floor(top)};
            }
            if(bounds.width()>0&&bounds.height()>0){
                hollowPolygon_=std::move(polygon);hollowBounds_=bounds;hollowBounded_=true;hollowDrag_.reset();emit hollowBoundsChanged();
                // Keep tool true while QML synchronizes text fields; restoring
                // its numeric Binding earlier would overwrite this selection.
                emit hollowSelectionFinished();hollowDragTool_=false;hollowLassoTool_=false;
                emit brushSettingsChanged();emit viewChanged();
            }else cancelHollowDrag();
        }
        e->accept();return;
    }
    if(!tabletStroke_&&!stroke_.empty()){const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));finishStroke();}e->accept();
}
void CanvasItem::wheelEvent(QWheelEvent* event){
    const auto delta=event->angleDelta().y();
    if(delta && zoomAt(std::pow(1.2,double(delta)/120.0),event->position().x(),event->position().y()))event->accept();
    else event->ignore();
}
bool CanvasItem::event(QEvent* event){
    if((bucketTool_ || hollowTool_ || closeGapTool_) && (event->type()==QEvent::TabletPress || event->type()==QEvent::TabletMove || event->type()==QEvent::TabletRelease)){event->accept();return true;}
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::WindowDeactivate){cancelStroke();brushPointer_.reset();emit footprintChanged();}
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){cancelStroke();event->accept();return true;}
    if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease){
        auto*t=static_cast<QTabletEvent*>(event);brushPointer_.reset();emit footprintChanged();auto sample=tabletSample(t);const bool isEraser=t->pointingDevice()&&t->pointingDevice()->pointerType()==QPointingDevice::PointerType::Eraser;
        const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;
        const bool inside=s&&itemToDocument(t->position(),*s).has_value();
        if(event->type()==QEvent::TabletPress){if(inside)beginSample(sample,true,isEraser);}else if(event->type()==QEvent::TabletMove){if(inside)appendSample(sample);}else{if(inside)appendSample(sample);finishStroke();tabletStroke_=false;}t->accept();return true;
    }return QQuickItem::event(event);
}
QSGNode* CanvasItem::updatePaintNode(QSGNode* old,UpdatePaintNodeData*){
    delete old;auto*root=new QSGNode;root->appendChildNode(new QSGSimpleRectNode(QRectF(0,0,width(),height()),QColor("#323235")));
    std::uint64_t count;double viewZoom;QPointF panOffset;std::optional<DocumentSnapshot> snap;QImage frame;bool legacy;
    {std::lock_guard lock(snapshotMutex_);snap=snapshot_;count=replayCount_;viewZoom=zoom_;panOffset=pan_;frame=displayFrame_;legacy=legacyDisplay_;}if(!snap||!window())return root;
    const auto viewTransform=documentTransform(*snap,viewZoom,panOffset);const auto scale=viewTransform.m11();if(!(scale>0))return root;
    const auto ox=viewTransform.dx(),oy=viewTransform.dy();
    root->appendChildNode(new QSGSimpleRectNode(QRectF(ox,oy,snap->spec.pixelSize.width*scale,snap->spec.pixelSize.height*scale),QColor("#faf9f6")));
    if(snap->spec.colorSpec.format.mode!=ColorMode::RGB||snap->spec.colorSpec.format.depth!=Depth::U8)return root;
    const auto nx=(snap->spec.pixelSize.width+TileSide-1)/TileSide,ny=(snap->spec.pixelSize.height+TileSide-1)/TileSide;EventLimit limit{snap->revision,std::min<std::uint64_t>(count,snap->eventIndex->size()),UINT64_MAX};
    auto* viewport=new QSGTransformNode;
    QMatrix4x4 view;view.translate(float(ox),float(oy));view.scale(float(scale));viewport->setMatrix(view);
    root->appendChildNode(viewport);
    auto* documentClip = new QSGClipNode;
    documentClip->setIsRectangular(true);
    documentClip->setClipRect(QRectF(0, 0, double(snap->spec.pixelSize.width), double(snap->spec.pixelSize.height)));
    viewport->appendChildNode(documentClip);
    if(!frame.isNull()){
        // GPU tiles respect texture limits. Software paints one image to avoid
        // fractional-scale seams between independently sampled tile edges.
        const int displayTileSide=window()->rendererInterface()->graphicsApi()==QSGRendererInterface::Software ? std::max(frame.width(),frame.height()) : int(TileSide);
        for(int y=0;y<frame.height();y+=displayTileSide)for(int x=0;x<frame.width();x+=displayTileSide){
            const QRect rect(x,y,std::min(displayTileSide,frame.width()-x),std::min(displayTileSide,frame.height()-y));
            // Full software frame is implicitly shared; only GPU subtiles need copying.
            const QImage textureImage=rect==frame.rect()?frame:frame.copy(rect);
            auto* texture=window()->createTextureFromImage(textureImage);
            if(!texture){
                const auto failedDocument=snap->documentId;const auto failedRevision=snap->revision;const auto failedFrameKey=frame.cacheKey();
                QMetaObject::invokeMethod(this,[this,failedDocument,failedRevision,failedFrameKey]{
                    {
                        std::lock_guard lock(snapshotMutex_);
                        if(!snapshot_ || snapshot_->documentId!=failedDocument || snapshot_->revision!=failedRevision || displayFrame_.cacheKey()!=failedFrameKey)return;
                        renderError_=QStringLiteral("canvas.texture-allocation");displayFrame_={};legacyDisplay_=false;
                    }
                    emit renderErrorChanged();update();
                },Qt::QueuedConnection);
                delete root;auto* failed=new QSGNode;
                failed->appendChildNode(new QSGSimpleRectNode(QRectF(0,0,width(),height()),QColor("#323235")));
                return failed;
            }
            auto* node=new QSGSimpleTextureNode;node->setOwnsTexture(true);node->setTexture(texture);
            node->setRect(QRectF(rect));documentClip->appendChildNode(node);
        }
        return root;
    }
    if(!legacy)return root;
    std::function<void(NodeId,QSGNode*)> appendNode=[&](NodeId id,QSGNode* parent){
        const auto& layer=snap->nodes->at(id);
        if(!layer.visible||layer.opacity<=0)return;
        if(layer.kind==NodeKind::Layer&&!nativeLayerSupported(*snap,id))return;
        auto* opacity=new QSGOpacityNode;opacity->setOpacity(float(layer.opacity));parent->appendChildNode(opacity);
        auto* transform=new QSGTransformNode;const auto&a=layer.transform.affine;
        transform->setMatrix(QMatrix4x4(float(a[0]),float(a[2]),0,float(a[4]),float(a[1]),float(a[3]),0,float(a[5]),0,0,1,0,0,0,0,1));
        opacity->appendChildNode(transform);
        if(layer.kind!=NodeKind::Layer){for(auto child:layer.children)appendNode(child,transform);return;}
        for(std::uint64_t y=0;y<ny;++y)for(std::uint64_t x=0;x<nx;++x){auto tile=renderer_.render(*snap,{id,std::int64_t(x),std::int64_t(y),0},limit);if(!tile)continue;auto masked=nativeMaskedTile(*snap,id,std::move(tile.value()),std::int64_t(x*TileSide),std::int64_t(y*TileSide));if(!masked)continue;const auto&t=masked.value();QImage image(int(t.width),int(t.height),QImage::Format_RGBA8888);for(std::uint32_t row=0;row<t.height;++row)std::memcpy(image.scanLine(int(row)),t.bytes.data()+row*t.rowStride,std::size_t(t.width)*4);auto*texture=window()->createTextureFromImage(image.convertToFormat(QImage::Format_RGBA8888_Premultiplied));if(!texture)continue;auto*node=new QSGSimpleTextureNode;node->setOwnsTexture(true);node->setTexture(texture);node->setRect(QRectF(x*TileSide,y*TileSide,t.width,t.height));transform->appendChildNode(node);}
    };
    appendNode(Document::rootId(),documentClip);
    return root;
}
}
#endif

