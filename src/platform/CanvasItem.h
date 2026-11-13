#pragma once
#ifdef DRAWBORAD_HAS_QT
#include "brush/BrushEngine.h"
#include "render/ReferenceRenderer.h"
#include "view/Viewport.h"
#include <QQuickItem>
#include <QImage>
#include <QColor>
#include <QTimer>
#include <QPointF>
#include <QPointer>
#include <QTransform>
#include <mutex>
#include <QThreadPool>
#include <stop_token>
namespace drawborad {
class QtBridge;
class CanvasItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* bridge READ studio WRITE setStudio NOTIFY studioChanged)
    Q_PROPERTY(bool hollowBounded READ hollowBounded WRITE setHollowBounded NOTIFY brushSettingsChanged)
    Q_PROPERTY(QRectF hollowBounds READ hollowBounds WRITE setHollowBounds NOTIFY brushSettingsChanged)
    Q_PROPERTY(QRectF hollowItemRect READ hollowItemRect NOTIFY viewChanged)
    Q_PROPERTY(bool hollowTool READ hollowTool WRITE setHollowTool NOTIFY brushSettingsChanged)
    Q_PROPERTY(bool bucketTool READ bucketTool WRITE setBucketTool NOTIFY brushSettingsChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool strokePreviewActive READ strokePreviewActive NOTIFY renderErrorChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY renderErrorChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderErrorChanged)
    Q_PROPERTY(QString renderError READ renderError NOTIFY renderErrorChanged)
    Q_PROPERTY(QRectF documentRect READ documentRect NOTIFY viewChanged)
    Q_PROPERTY(double brushWidth READ brushWidth NOTIFY brushSettingsChanged)
    Q_PROPERTY(double brushOpacity READ brushOpacity NOTIFY brushSettingsChanged)
    Q_PROPERTY(double brushHardness READ brushHardness NOTIFY brushSettingsChanged)
    Q_PROPERTY(double mouseTaper READ mouseTaper NOTIFY brushSettingsChanged)
    Q_PROPERTY(QColor brushColor READ brushColor NOTIFY brushSettingsChanged)
    Q_PROPERTY(QString brushNotice READ brushNotice NOTIFY brushSettingsChanged)
    BrushSettings brushSettings_,gestureBrush_;
    QColor brushColor_{QColor::fromRgbF(.08,.08,.08)},gestureColor_;
    bool bucketTool_{false},hollowTool_{false},hollowBounded_{false};
    QRectF hollowBounds_{0,0,64,64};
    double mouseTaper_{0},gestureTaper_{0};
    QString brushNotice_;
    QPointer<QtBridge> bridge_;
    mutable std::mutex snapshotMutex_;
    std::optional<DocumentSnapshot> snapshot_;
    QImage displayFrame_;
    QImage committedFrame_;
    bool previewing_{false};
    std::uint64_t previewRequestSequence_{0},completedPreviewSequence_{0};
    QTimer previewTimer_;
    QImage completedFrame_;
    QString completedError_;
    bool completedLegacy_{false};
    QString renderError_;
    bool legacyDisplay_{false};
    bool rendering_{false};
    QThreadPool renderPool_;
    std::stop_source renderStop_;
    std::uint64_t renderGeneration_{0};
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
    void requestPreview();
    void clearPreview();
    BrushSettings currentGestureSettings(const DocumentSnapshot&)const;
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
    void mouseUngrabEvent() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void geometryChange(const QRectF&,const QRectF&) override;
    bool event(QEvent*) override;
  signals:
    void brushSettingsChanged();
    void studioChanged();
    void viewChanged();
    void renderErrorChanged();
  public:
    bool hollowBounded() const { return hollowBounded_; }
    void setHollowBounded(bool value) { hollowBounded_=value;emit brushSettingsChanged();emit viewChanged(); }
    QRectF hollowBounds() const { return hollowBounds_; }
    void setHollowBounds(QRectF value) { hollowBounds_=value;emit brushSettingsChanged();emit viewChanged(); }
    QRectF hollowItemRect() const;
    bool hollowTool() const { return hollowTool_; }
    void setHollowTool(bool value) { cancelStroke();hollowTool_=value;if(value)bucketTool_=false;emit brushSettingsChanged(); }
    bool bucketTool() const { return bucketTool_; }
    void setBucketTool(bool value) { cancelStroke();bucketTool_=value;if(value)hollowTool_=false;emit brushSettingsChanged(); }
    explicit CanvasItem(QQuickItem* parent = nullptr);
    ~CanvasItem() override;
    bool rendering() const;
    bool previewing() const;
    bool strokePreviewActive()const{return !stroke_.empty();}
    Q_INVOKABLE void cancelStroke();
    double brushWidth()const{return brushSettings_.width;}
    double brushOpacity()const{return brushSettings_.opacity;}
    double brushHardness()const{return brushSettings_.hardness;}
    double mouseTaper()const{return mouseTaper_;}
    QColor brushColor()const{return brushColor_;}
    QString brushNotice()const{return brushNotice_;}
    Q_INVOKABLE bool configureBrush(double width,QColor color,double opacity,double hardness,double taper);
    double zoom() const;
    QString renderError() const;
    QRectF documentRect() const;
    Q_INVOKABLE bool zoomAt(double factor,double x,double y);
    Q_INVOKABLE bool panBy(double x,double y);
    Q_INVOKABLE void fitDocument();
};
}
#endif

