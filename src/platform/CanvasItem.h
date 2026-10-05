#pragma once
#ifdef DRAWBORAD_HAS_QT
#include "brush/BrushEngine.h"
#include "render/ReferenceRenderer.h"
#include "view/Viewport.h"
#include <QQuickItem>
#include <QPointF>
#include <QPointer>
#include <mutex>
namespace drawborad {
class QtBridge;
class CanvasItem final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* bridge READ studio WRITE setStudio NOTIFY studioChanged)
    QPointer<QtBridge> bridge_;
    mutable std::mutex snapshotMutex_;
    std::optional<DocumentSnapshot> snapshot_;
    ReferenceRenderer renderer_;
    BrushEngine brush_;
    std::vector<InputSample> stroke_;
    bool tabletStroke_ = false;
    bool eraser_ = false;
    QObject* studio() const;
    void setStudio(QObject*);
    void refreshSnapshot();
    std::optional<std::pair<NodeId, NodeId>> paintTarget(const DocumentSnapshot&) const;
    std::optional<DocumentPoint> itemToDocument(const QPointF&, const DocumentSnapshot&) const;
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
    bool event(QEvent*) override;
  signals:
    void studioChanged();
  public:
    explicit CanvasItem(QQuickItem* parent = nullptr);
};
}
#endif
