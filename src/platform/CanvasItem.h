#pragma once
#ifdef DRAWBORAD_HAS_QT
#include "brush/BrushEngine.h"
#include "render/ReferenceRenderer.h"
#include "view/Viewport.h"
#include <QQuickItem>
#include <QPointF>
#include <QPointer>
#include <QTransform>
#include <mutex>
namespace drawborad {
class QtBridge;
class CanvasItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* bridge READ studio WRITE setStudio NOTIFY studioChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(QRectF documentRect READ documentRect NOTIFY viewChanged)
    QPointer<QtBridge> bridge_;
    mutable std::mutex snapshotMutex_;
    std::optional<DocumentSnapshot> snapshot_;
    std::uint64_t replayCount_{UINT64_MAX};
    double zoom_{1};
    QPointF pan_;
    std::optional<QPointF> panGesture_;
    ReferenceRenderer renderer_;
    BrushEngine brush_;
    std::vector<InputSample> stroke_;
    struct StrokeTarget {
        DocumentId document;
        NodeId object, layer;
        std::uint64_t generation{};
    };
    std::optional<StrokeTarget> strokeTarget_;
    bool tabletStroke_ = false;
    bool eraser_ = false;
    QObject* studio() const;
    void setStudio(QObject*);
    void refreshSnapshot();
    std::optional<std::pair<NodeId, NodeId>> paintTarget(const DocumentSnapshot&) const;
    std::optional<DocumentPoint> itemToDocument(const QPointF&, const DocumentSnapshot&) const;
    QTransform documentTransform(const DocumentSnapshot&,double,QPointF) const;
    QPointF boundedPan(const DocumentSnapshot&,double,QPointF) const;
    void beginSample(InputSample, bool tablet, bool eraser);
    void appendSample(InputSample);
    void finishStroke();
    InputSample mouseSample(class QMouseEvent*) const;
    InputSample tabletSample(class QTabletEvent*) const;
  protected:
    QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void geometryChange(const QRectF&,const QRectF&) override;
    bool event(QEvent*) override;
  signals:
    void studioChanged();
    void viewChanged();
  public:
    explicit CanvasItem(QQuickItem* parent = nullptr);
    double zoom() const;
    QRectF documentRect() const;
    Q_INVOKABLE bool zoomAt(double factor,double x,double y);
    Q_INVOKABLE bool panBy(double x,double y);
    Q_INVOKABLE void fitDocument();
};
}
#endif
