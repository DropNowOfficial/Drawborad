#include "CanvasItem.h"
#ifdef DRAWBORAD_HAS_QT
#include "apps/studio/QtBridge.h"
#include "view/Viewport.h"
#include <QInputDevice>
#include <QMouseEvent>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QTabletEvent>
#include <QQuickWindow>
#include <algorithm>
#include <cstring>
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
    bridge_ = next;
    if (bridge_) connect(bridge_, &QtBridge::changed, this, &CanvasItem::refreshSnapshot);
    refreshSnapshot();
    emit studioChanged();
}
void CanvasItem::refreshSnapshot() {
    std::lock_guard lock(snapshotMutex_);
    snapshot_ = bridge_ ? bridge_->canvasSnapshot() : std::nullopt;
    update();
}
std::optional<std::pair<NodeId, NodeId>> CanvasItem::paintTarget(const DocumentSnapshot& s) const {
    for (const auto& [id, n] : *s.nodes)
        if (n.kind == NodeKind::Layer && n.visible && !n.locked && n.sourceKind == SourceKind::Reconstructed)
            return std::pair{n.objectId, id};
    return std::nullopt;
}
std::optional<DocumentPoint> CanvasItem::itemToDocument(const QPointF& p,const DocumentSnapshot& s) const {
    if (width() <= 0 || height() <= 0) return std::nullopt;
    const double scale = std::min(width()/double(s.spec.pixelSize.width),height()/double(s.spec.pixelSize.height));
    if (!(scale > 0)) return std::nullopt;
    const double ox=(width()-double(s.spec.pixelSize.width)*scale)*.5;
    const double oy=(height()-double(s.spec.pixelSize.height)*scale)*.5;
    DocumentPoint d{(p.x()-ox)/scale,(p.y()-oy)/scale};
    if(d.x<0||d.y<0||d.x>=double(s.spec.pixelSize.width)||d.y>=double(s.spec.pixelSize.height))return std::nullopt;
    return d;
}
InputSample CanvasItem::mouseSample(QMouseEvent* e) const {
    InputSample s; std::optional<DocumentSnapshot> snap; {std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(snap)if(auto p=itemToDocument(e->position(),*snap)){s.x=p->x;s.y=p->y;}
    s.pressure=1.0;s.timestampUs=std::uint64_t(e->timestamp())*1000;s.deviceKind=std::uint32_t(QInputDevice::DeviceType::Mouse);s.capabilities=0;return s;
}
InputSample CanvasItem::tabletSample(QTabletEvent* e) const {
    TabletPacket p;std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(snap)if(auto d=itemToDocument(e->position(),*snap)){p.x=d->x;p.y=d->y;}
    p.timestampUs=std::uint64_t(e->timestamp())*1000;p.deviceKind=std::uint32_t(e->device()->type());
    const auto dev=e->device();
    if(dev&&dev->hasCapability(QInputDevice::Capability::Pressure)){p.capabilities|=PressureCap;p.pressure=e->pressure();}
    if(dev&&(dev->hasCapability(QInputDevice::Capability::XTilt)||dev->hasCapability(QInputDevice::Capability::YTilt))){p.capabilities|=TiltCap;p.tiltX=e->xTilt();p.tiltY=e->yTilt();}
    if(e->pointingDevice()&&e->pointingDevice()->pointerType()==QPointingDevice::PointerType::Eraser){p.capabilities|=EraserCap;p.eraser=true;}
    eraser_=p.eraser;return normalizeTabletSample(p);
}
void CanvasItem::beginSample(InputSample s,bool tablet,bool eraser){stroke_.clear();stroke_.push_back(s);tabletStroke_=tablet;eraser_=eraser;}
void CanvasItem::appendSample(InputSample s){if(!stroke_.empty())stroke_.push_back(s);}
void CanvasItem::finishStroke(){
    if(!bridge_||stroke_.empty()){stroke_.clear();return;}std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}
    if(!snap){stroke_.clear();return;}auto target=paintTarget(*snap);if(!target){stroke_.clear();return;}
    BrushSettings b;b.objectId=target->first;b.layerId=target->second;b.parentRevision=snap->revision;b.toolId="pencil";b.width=12;b.opacity=1;b.hardness=.8;b.spacing=.2;b.seed=stroke_.front().timestampUs^(std::uint64_t(stroke_.size())<<32);b.color.profileId=snap->spec.colorSpec.profileId;b.color.activeChannels=snap->spec.colorSpec.format.mode==ColorMode::CMYK?4:3;b.color.channels={0.08,0.08,0.08,0.08};b.color.alpha=1;
    auto event=brush_.compile(stroke_,b,eraser_);stroke_.clear();if(event)bridge_->commitStroke(event.value());
}
void CanvasItem::mousePressEvent(QMouseEvent* e){if(tabletStroke_){e->ignore();return;}beginSample(mouseSample(e),false,false);e->accept();}
void CanvasItem::mouseMoveEvent(QMouseEvent* e){if(!tabletStroke_&&!stroke_.empty())appendSample(mouseSample(e));e->accept();}
void CanvasItem::mouseReleaseEvent(QMouseEvent* e){if(!tabletStroke_&&!stroke_.empty()){appendSample(mouseSample(e));finishStroke();}e->accept();}
bool CanvasItem::event(QEvent* event){
    if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease){
        auto*t=static_cast<QTabletEvent*>(event);auto sample=tabletSample(t);const bool isEraser=t->pointingDevice()&&t->pointingDevice()->pointerType()==QPointingDevice::PointerType::Eraser;
        if(event->type()==QEvent::TabletPress)beginSample(sample,true,isEraser);else if(event->type()==QEvent::TabletMove)appendSample(sample);else{appendSample(sample);finishStroke();tabletStroke_=false;}t->accept();return true;
    }return QQuickItem::event(event);
}
QSGNode* CanvasItem::updatePaintNode(QSGNode* old,UpdatePaintNodeData*){
    delete old;auto*root=new QSGNode;root->appendChildNode(new QSGSimpleRectNode(QRectF(0,0,width(),height()),QColor("#323235")));
    std::optional<DocumentSnapshot> snap;{std::lock_guard lock(snapshotMutex_);snap=snapshot_;}if(!snap||!window())return root;
    const double scale=std::min(width()/double(snap->spec.pixelSize.width),height()/double(snap->spec.pixelSize.height));if(!(scale>0))return root;
    const double ox=(width()-double(snap->spec.pixelSize.width)*scale)*.5,oy=(height()-double(snap->spec.pixelSize.height)*scale)*.5;
    root->appendChildNode(new QSGSimpleRectNode(QRectF(ox,oy,snap->spec.pixelSize.width*scale,snap->spec.pixelSize.height*scale),QColor("#faf9f6")));
    auto target=paintTarget(*snap);if(!target||snap->spec.colorSpec.format.mode!=ColorMode::RGB||snap->spec.colorSpec.format.depth!=Depth::U8)return root;
    const auto nx=(snap->spec.pixelSize.width+TileSide-1)/TileSide,ny=(snap->spec.pixelSize.height+TileSide-1)/TileSide;EventLimit limit{snap->revision,snap->eventIndex->size(),UINT64_MAX};
    for(std::uint64_t y=0;y<ny;++y)for(std::uint64_t x=0;x<nx;++x){auto tile=renderer_.render(*snap,{target->second,std::int64_t(x),std::int64_t(y),0},limit);if(!tile)continue;const auto&t=tile.value();QImage image(int(t.width),int(t.height),QImage::Format_RGBA8888);for(std::uint32_t row=0;row<t.height;++row)std::memcpy(image.scanLine(int(row)),t.bytes.data()+row*t.rowStride,std::size_t(t.width)*4);auto*texture=window()->createTextureFromImage(image);if(!texture)continue;auto*node=new QSGSimpleTextureNode;node->setOwnsTexture(true);node->setTexture(texture);node->setRect(QRectF(ox+x*TileSide*scale,oy+y*TileSide*scale,t.width*scale,t.height*scale));root->appendChildNode(node);}
    return root;
}
}
#endif
