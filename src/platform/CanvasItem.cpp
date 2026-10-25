#include "CanvasItem.h"
#ifdef DRAWBORAD_HAS_QT
#include "apps/studio/QtBridge.h"
#include "view/Viewport.h"
#include "view/LayerTree.h"
#include <QInputDevice>
#include <QMouseEvent>
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
#include <cstring>
#include <functional>
namespace drawborad {
CanvasItem::CanvasItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
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
    std::lock_guard lock(snapshotMutex_);
    if (snapshot_ && next && snapshot_->documentId == next->documentId &&
        snapshot_->revision == next->revision && snapshot_->nodes == next->nodes &&
        snapshot_->eventIndex == next->eventIndex && replayCount_==nextCount) return;
    snapshot_ = next;
    replayCount_=nextCount;
    update();
}
std::optional<std::pair<NodeId, NodeId>> CanvasItem::paintTarget(const DocumentSnapshot& s) const {
    if (bridge_)
        if (const auto id = bridge_->activeLayer(); id && nativeLayerPaintable(s, *id))
            return std::pair{s.nodes->at(*id).objectId, *id};
    return std::nullopt;
}
std::optional<DocumentPoint> CanvasItem::itemToDocument(const QPointF& p,const DocumentSnapshot& s) const {
    if (width() <= 0 || height() <= 0) return std::nullopt;
    const double scale = std::min(width()/double(s.spec.pixelSize.width),height()/double(s.spec.pixelSize.height));
    if (!(scale > 0)) return std::nullopt;
    const double ox=(width()-double(s.spec.pixelSize.width)*scale)*.5;
    const double oy=(height()-double(s.spec.pixelSize.height)*scale)*.5;
    DocumentPoint docPoint{(p.x()-ox)/scale,(p.y()-oy)/scale};
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
    if (!bridge_ || bridge_->busy() || bridge_->replaying()) return;
    const auto snapshot = bridge_->canvasSnapshot();
    if (!snapshot) return;
    const auto target = paintTarget(*snapshot);
    if (!target) return;
    strokeTarget_ = StrokeTarget{snapshot->documentId, target->first, target->second, bridge_->canvasGeneration()};
    stroke_.push_back(sample);
}
void CanvasItem::appendSample(InputSample s){if(!stroke_.empty())stroke_.push_back(s);}
void CanvasItem::finishStroke(){
    if(!bridge_||stroke_.empty()){stroke_.clear();return;}std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
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
void CanvasItem::mousePressEvent(QMouseEvent* e){const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(tabletStroke_||!s||!itemToDocument(e->position(),*s)){e->ignore();return;}beginSample(mouseSample(e),false,false);e->accept();}
void CanvasItem::mouseMoveEvent(QMouseEvent* e){const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(!tabletStroke_&&!stroke_.empty()&&s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));e->accept();}
void CanvasItem::mouseReleaseEvent(QMouseEvent* e){if(!tabletStroke_&&!stroke_.empty()){const auto s=bridge_?bridge_->canvasSnapshot():std::nullopt;if(s&&itemToDocument(e->position(),*s))appendSample(mouseSample(e));finishStroke();}e->accept();}
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
    std::uint64_t count;std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;count=replayCount_;}if(!snap||!window())return root;
    const double scale=std::min(width()/double(snap->spec.pixelSize.width),height()/double(snap->spec.pixelSize.height));if(!(scale>0))return root;
    const double ox=(width()-double(snap->spec.pixelSize.width)*scale)*.5,oy=(height()-double(snap->spec.pixelSize.height)*scale)*.5;
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
