#include "CanvasItem.h"
#include "CanvasFrame.h"
#ifdef DRAWBORAD_HAS_QT
#include "apps/studio/QtBridge.h"
#include "view/Viewport.h"
#include "view/LayerTree.h"
#include <QInputDevice>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
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
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setClip(true);
}
QObject* CanvasItem::studio() const { return bridge_; }
void CanvasItem::setStudio(QObject* value) {
    auto next = qobject_cast<QtBridge*>(value);
    if (bridge_ == next) return;
    if (bridge_) disconnect(bridge_, nullptr, this, nullptr);
    stroke_.clear();
    strokeTarget_.reset();
    bridge_ = next;
    if (bridge_) connect(bridge_, &QtBridge::changed, this, &CanvasItem::refreshSnapshot);
    refreshSnapshot();
    emit studioChanged();
}
void CanvasItem::refreshSnapshot() {
    const auto next = bridge_ ? bridge_->canvasSnapshot() : std::nullopt;
    const auto nextCount=bridge_?std::uint64_t(bridge_->replayCount()):UINT64_MAX;
    if (strokeTarget_ && (!bridge_ || !next || strokeTarget_->document != next->documentId ||
                          strokeTarget_->generation != bridge_->canvasGeneration())) {
        stroke_.clear();
        strokeTarget_.reset();
        tabletStroke_ = false;
    }
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_ && next && snapshot_->documentId == next->documentId &&
            snapshot_->revision == next->revision && snapshot_->nodes == next->nodes &&
            snapshot_->eventIndex == next->eventIndex && replayCount_==nextCount) return;
    }
    QImage frame;QString failure;bool legacy=false;
    if(next){
        auto rendered=renderCanvasFrame(*next,nextCount);
        if(rendered)frame=std::move(rendered.value());
        else {
            // Preserve transformed native-only documents handled by the old scene graph.
            // Never fall back when a raster layer would silently disappear.
            legacy=rendered.error().messageKey=="merged.native-compositing-required" &&
                std::all_of(next->nodes->begin(),next->nodes->end(),[&](const auto& entry){
                    return entry.second.kind!=NodeKind::Layer || !layerVisible(*next,entry.first) || nativeLayerSupported(*next,entry.first);
                });
            if(!legacy)failure=QString::fromStdString(rendered.error().messageKey);
        }
    }
    {
        std::lock_guard lock(snapshotMutex_);
        if(!snapshot_ || !next || snapshot_->documentId!=next->documentId || snapshot_->spec.pixelSize!=next->spec.pixelSize){
            zoom_=1;pan_={};panGesture_.reset();
        }
        snapshot_=next;replayCount_=nextCount;displayFrame_=std::move(frame);
        renderError_=failure;legacyDisplay_=legacy;
    }
    emit renderErrorChanged();
    update();emit viewChanged();
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
    stroke_.clear();strokeTarget_.reset();tabletStroke_=false;
    update();emit viewChanged();return true;
}
bool CanvasItem::panBy(double x,double y){
    if(!std::isfinite(x) || !std::isfinite(y))return false;
    {
        std::lock_guard lock(snapshotMutex_);if(!snapshot_)return false;
        pan_=boundedPan(*snapshot_,zoom_,pan_+QPointF{x,y});
    }
    stroke_.clear();strokeTarget_.reset();tabletStroke_=false;
    update();emit viewChanged();return true;
}
void CanvasItem::fitDocument(){
    {std::lock_guard lock(snapshotMutex_);zoom_=1;pan_={};}
    stroke_.clear();strokeTarget_.reset();tabletStroke_=false;panGesture_.reset();
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
    if(!invertible)return std::nullopt;const auto point=inverse.map(p);
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
    stroke_.clear();
    strokeTarget_.reset();
    tabletStroke_ = tablet;
    eraser_ = eraser;
    if (!bridge_ || bridge_->busy() || bridge_->replaying() || !renderError().isEmpty()) return;
    const auto snapshot = bridge_->canvasSnapshot();
    if (!snapshot) return;
    const auto target = paintTarget(*snapshot);
    if (!target) return;
    strokeTarget_ = StrokeTarget{snapshot->documentId, target->first, target->second, bridge_->canvasGeneration()};
    stroke_.push_back(sample);
}
void CanvasItem::appendSample(InputSample s){if(!stroke_.empty())stroke_.push_back(s);}
void CanvasItem::finishStroke(){
    if(!bridge_||stroke_.empty()||!renderError().isEmpty()){stroke_.clear();strokeTarget_.reset();return;}std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(!snap||!strokeTarget_||strokeTarget_->document!=snap->documentId||
       strokeTarget_->generation!=bridge_->canvasGeneration()||
       !nativeLayerPaintable(*snap,strokeTarget_->layer)||
       snap->nodes->at(strokeTarget_->layer).objectId!=strokeTarget_->object){stroke_.clear();strokeTarget_.reset();return;}
    const auto target=*strokeTarget_;
    BrushSettings b;b.objectId=target.object;b.layerId=target.layer;b.parentRevision=snap->revision;b.toolId="pencil";b.width=12;b.opacity=1;b.hardness=.8;b.spacing=.2;b.seed=stroke_.front().timestampUs^(std::uint64_t(stroke_.size())<<32);b.color.profileId=snap->spec.colorSpec.profileId;b.color.activeChannels=snap->spec.colorSpec.format.mode==ColorMode::CMYK?4:3;b.color.channels={0.08,0.08,0.08,0.08};b.color.alpha=1;
    auto event=brush_.compile(stroke_,b,eraser_);
    stroke_.clear();
    strokeTarget_.reset();
    if (event) {
        // The engine's deterministic ID can repeat for equal-length input. A new
        // user gesture needs its own identity, including after reopening a file.
        const auto bytes = QUuid::createUuid().toRfc4122();
        std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()), 16,
                    event.value().id.bytes.begin());
        (void)bridge_->commitStroke(event.value());
    }
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
        // Texture tiles avoid hardware maximum texture dimensions on print-size canvases.
        for(int y=0;y<frame.height();y+=int(TileSide))for(int x=0;x<frame.width();x+=int(TileSide)){
            const QRect rect(x,y,std::min(int(TileSide),frame.width()-x),std::min(int(TileSide),frame.height()-y));
            auto* texture=window()->createTextureFromImage(frame.copy(rect).convertToFormat(QImage::Format_RGBA8888_Premultiplied));
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

