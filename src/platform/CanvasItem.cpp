#include "CanvasItem.h"
#include "CanvasFrame.h"
#include "brush/PressureTaper.h"
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
    brushSettings_.width=12;brushSettings_.spacing=.2;
    renderPool_.setMaxThreadCount(1);
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setClip(true);
}
CanvasItem::~CanvasItem(){renderStop_.request_stop();renderPool_.clear();renderPool_.waitForDone();}
bool CanvasItem::previewing() const {std::lock_guard lock(snapshotMutex_);return previewing_;}
bool CanvasItem::rendering() const {std::lock_guard lock(snapshotMutex_);return rendering_;}
bool CanvasItem::configureBrush(double width,QColor color,double opacity,double hardness,double taper){
    if(!std::isfinite(width)||width<.5||width>512||!color.isValid()||!std::isfinite(opacity)||opacity<.01||opacity>1||!std::isfinite(hardness)||hardness<0||hardness>1||!std::isfinite(taper)||taper<0||taper>.5)return false;
    brushSettings_.width=width;brushSettings_.opacity=opacity;brushSettings_.hardness=hardness;brushColor_=color;mouseTaper_=taper;brushNotice_.clear();emit brushSettingsChanged();return true;
}
QObject* CanvasItem::studio() const { return bridge_; }
void CanvasItem::setStudio(QObject* value) {
    auto next = qobject_cast<QtBridge*>(value);
    if (bridge_ == next) return;
    if (bridge_) disconnect(bridge_, nullptr, this, nullptr);
    cancelStroke();
    bridge_ = next;
    if (bridge_) connect(bridge_, &QtBridge::changed, this, &CanvasItem::refreshSnapshot);
    refreshSnapshot();
    emit studioChanged();
}
void CanvasItem::refreshSnapshot() {
    const auto next = bridge_ ? bridge_->canvasSnapshot() : std::nullopt;
    const auto nextCount=bridge_?std::uint64_t(bridge_->replayCount()):UINT64_MAX;
    if(bridge_&&(bridge_->busy()||bridge_->replaying()))cancelStroke();
    if (strokeTarget_ && (!bridge_ || !next || strokeTarget_->document != next->documentId ||
                          strokeTarget_->generation != bridge_->canvasGeneration())) {
        cancelStroke();
    }
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_ && next && snapshot_->documentId == next->documentId &&
            snapshot_->revision == next->revision && snapshot_->nodes == next->nodes &&
            snapshot_->eventIndex == next->eventIndex && replayCount_==nextCount) return;
    }
    // A changed document/replay invalidates an in-progress input gesture.
    previewTimer_.stop();stroke_.clear();strokeTarget_.reset();tabletStroke_=false;
    renderStop_.request_stop();renderPool_.clear();renderStop_=std::stop_source{};
    const auto token=renderStop_.get_token();std::uint64_t generation;
    {
        std::lock_guard lock(snapshotMutex_);
        if(!snapshot_ || !next || snapshot_->documentId!=next->documentId || snapshot_->spec.pixelSize!=next->spec.pixelSize){zoom_=1;pan_={};panGesture_.reset();}
        generation=++renderGeneration_;completedFrame_={};completedError_={};
        snapshot_=next;replayCount_=nextCount;previewing_=false;committedFrame_={};displayFrame_={};renderError_={};legacyDisplay_=false;rendering_=bool(next);
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
    if(docPoint.x<0||docPoint.y<0||docPoint.x>=double(s.spec.pixelSize.width)||docPoint.y>=double(s.spec.pixelSize.height))return std::nullopt;
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
    eraser_ = eraser;
    if (!bridge_ || bridge_->busy() || bridge_->replaying() || rendering() || !renderError().isEmpty()) return;
    const auto snapshot = bridge_->canvasSnapshot();
    if (!snapshot) return;
    if(snapshot->spec.colorSpec.format.mode!=ColorMode::RGB){brushNotice_=QStringLiteral("RGB brush controls cannot paint a CMYK document without color conversion.");emit brushSettingsChanged();return;}
    const auto target = paintTarget(*snapshot);
    if (!target) return;
    strokeTarget_ = StrokeTarget{snapshot->documentId, target->first, target->second, bridge_->canvasGeneration()};
    brushNotice_.clear();emit brushSettingsChanged();
    renderStop_=std::stop_source{};
    gestureBrush_=brushSettings_;gestureBrush_.seed=sample.timestampUs;gestureColor_=brushColor_;gestureTaper_=mouseTaper_;
    stroke_.push_back(sample);setFocus(true,Qt::MouseFocusReason);previewTimer_.start();emit renderErrorChanged();
}
void CanvasItem::appendSample(InputSample s){if(!stroke_.empty()){stroke_.push_back(s);if(!previewTimer_.isActive())previewTimer_.start();}}
BrushSettings CanvasItem::currentGestureSettings(const DocumentSnapshot& snapshot)const{
    BrushSettings b=gestureBrush_;b.objectId=strokeTarget_->object;b.layerId=strokeTarget_->layer;b.parentRevision=snapshot.revision;
    b.color.profileId=snapshot.spec.colorSpec.profileId;b.color.activeChannels=3;b.color.channels={gestureColor_.redF(),gestureColor_.greenF(),gestureColor_.blueF(),0};b.color.alpha=gestureColor_.alphaF();return b;
}
void CanvasItem::clearPreview(){
    previewTimer_.stop();renderStop_.request_stop();renderPool_.clear();
    {std::lock_guard lock(snapshotMutex_);++renderGeneration_;completedFrame_={};displayFrame_=committedFrame_;previewing_=false;}
    emit renderErrorChanged();update();
}
void CanvasItem::cancelStroke(){if(stroke_.empty()&&!previewing()&&!previewTimer_.isActive())return;stroke_.clear();strokeTarget_.reset();tabletStroke_=false;clearPreview();}
void CanvasItem::mouseUngrabEvent(){cancelStroke();}
void CanvasItem::requestPreview() try{
    if(!bridge_||bridge_->busy()||bridge_->replaying()||stroke_.empty()||!strokeTarget_||rendering())return;
    const auto source=bridge_->canvasSnapshot();if(!source)return;
    const auto settings=currentGestureSettings(*source);const auto samples=stroke_;const auto taper=tabletStroke_?0:gestureTaper_;const auto eraser=eraser_;
    // Do not starve a running preview during continuous input. Replace only queued
    // work, then display completed snapshots in order until the newest catches up.
    renderPool_.clear();const auto token=renderStop_.get_token();std::uint64_t generation,sequence;
    {std::lock_guard lock(snapshotMutex_);generation=renderGeneration_;sequence=++previewRequestSequence_;previewing_=true;}
    emit renderErrorChanged();
    renderPool_.start([this,source,settings,samples,taper,eraser,token,generation,sequence]{
        if(token.stop_requested())return;
        QImage frame;QString failure;
        try{
        auto input=samples;if(taper>0){auto tapered=taperMousePressure(input,taper,true);if(tapered)input=std::move(tapered.value());}
        BrushEngine engine;auto event=engine.compile(input,settings,eraser);
        if(event){
            auto bytes=QUuid::createUuid().toRfc4122();std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()),16,event.value().id.bytes.begin());
            auto document=Document::fromSnapshot(*source);auto bounds=strokeBounds(event.value());
            if(document&&bounds){auto applied=document.value().apply(Command{Paint{event.value(),bounds.value()}},source->revision);
                if(applied&&!token.stop_requested()){auto rendered=renderCanvasFrame(document.value().snapshot(),UINT64_MAX,{},token);if(rendered)frame=rendered.value().convertToFormat(QImage::Format_RGBA8888_Premultiplied);else failure=QString::fromStdString(rendered.error().messageKey);}
            }
        }
        }catch(const std::bad_alloc&){failure=QStringLiteral("preview.allocation");}
        if(token.stop_requested())return;
        if(frame.isNull()&&failure.isEmpty())failure=QStringLiteral("Preview unavailable; committed document unchanged.");
        {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_)return;completedFrame_=std::move(frame);completedError_=failure;completedPreviewSequence_=sequence;}
        QMetaObject::invokeMethod(this,[this,generation,sequence]{
            {std::lock_guard lock(snapshotMutex_);if(generation!=renderGeneration_||sequence!=completedPreviewSequence_)return;if(!completedFrame_.isNull())displayFrame_=std::move(completedFrame_);previewing_=sequence<previewRequestSequence_;brushNotice_=completedError_;}
            emit renderErrorChanged();emit brushSettingsChanged();update();
        },Qt::QueuedConnection);
    });
}catch(const std::bad_alloc&){clearPreview();renderStop_=std::stop_source{};brushNotice_=QStringLiteral("Preview allocation failed; stroke input retained until release.");emit brushSettingsChanged();}
void CanvasItem::finishStroke(){
    if(!bridge_||bridge_->busy()||bridge_->replaying()||stroke_.empty()||rendering()||!renderError().isEmpty()){cancelStroke();return;}std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(!snap||!strokeTarget_||strokeTarget_->document!=snap->documentId||
       strokeTarget_->generation!=bridge_->canvasGeneration()||
       !nativeLayerPaintable(*snap,strokeTarget_->layer)||
       snap->nodes->at(strokeTarget_->layer).objectId!=strokeTarget_->object){cancelStroke();return;}
    BrushSettings b=currentGestureSettings(*snap);
    if(!tabletStroke_&&gestureTaper_>0){auto tapered=taperMousePressure(stroke_,gestureTaper_,true);if(tapered)stroke_=std::move(tapered.value());else {brushNotice_=QStringLiteral("Stroke retained without mouse taper: input exceeds taper limits.");emit brushSettingsChanged();}}
    clearPreview();
    auto event=brush_.compile(stroke_,b,eraser_);
    stroke_.clear();
    strokeTarget_.reset();
    emit renderErrorChanged();
    if (event) {
        // The engine's deterministic ID can repeat for equal-length input. A new
        // user gesture needs its own identity, including after reopening a file.
        const auto bytes = QUuid::createUuid().toRfc4122();
        std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()), 16,
                    event.value().id.bytes.begin());
        auto committed=bridge_->commitStroke(event.value());
        if(!committed){brushNotice_=tr("笔迹未提交，工程未修改：")+QString::fromStdString(committed.error().messageKey);emit brushSettingsChanged();}
    }else{brushNotice_=tr("笔迹未提交，工程未修改：")+QString::fromStdString(event.error().messageKey);emit brushSettingsChanged();}
}
void CanvasItem::mousePressEvent(QMouseEvent* e){
    if(e->button()==Qt::MiddleButton){panGesture_=e->position();e->accept();return;}
    const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(tabletStroke_||!s||!itemToDocument(e->position(),*s)){e->ignore();return;}beginSample(mouseSample(e),false,false);e->accept();
}
void CanvasItem::mouseMoveEvent(QMouseEvent* e){
    if(panGesture_){const auto delta=e->position()-*panGesture_;panGesture_=e->position();(void)panBy(delta.x(),delta.y());e->accept();return;}
    const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!tabletStroke_&&!stroke_.empty()&&s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));e->accept();
}
void CanvasItem::mouseReleaseEvent(QMouseEvent* e){
    if(e->button()==Qt::MiddleButton){panGesture_.reset();e->accept();return;}
    if(!tabletStroke_&&!stroke_.empty()){const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));finishStroke();}e->accept();
}
void CanvasItem::wheelEvent(QWheelEvent* event){
    const auto delta=event->angleDelta().y();
    if(delta && zoomAt(std::pow(1.2,double(delta)/120.0),event->position().x(),event->position().y()))event->accept();
    else event->ignore();
}
bool CanvasItem::event(QEvent* event){
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::WindowDeactivate)cancelStroke();
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){cancelStroke();event->accept();return true;}
    if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease){
        auto*t=static_cast<QTabletEvent*>(event);auto sample=tabletSample(t);const bool isEraser=t->pointingDevice()&&t->pointingDevice()->pointerType()==QPointingDevice::PointerType::Eraser;
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

