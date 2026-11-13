#pragma once
#include "ApplicationController.h"
#include "jobs/WinWorkerHost.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QColor>
#include <QRectF>
#include "view/Viewport.h"
#include <QThreadPool>
#include <stop_token>
#include <QVariantList>
#include <QImage>
namespace drawborad {
class WorkflowController;
class QtBridge : public QObject {
    Q_OBJECT
    friend class WorkflowController;
    Q_PROPERTY(QUrl originalReferenceUrl READ originalReferenceUrl NOTIFY changed)
    Q_PROPERTY(QString originalReferenceInfo READ originalReferenceInfo NOTIFY changed)
    Q_PROPERTY(QString originalReferenceNotice READ originalReferenceNotice NOTIFY changed)
    Q_PROPERTY(QObject* workflow READ workflow CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString dimensions READ dimensions NOTIFY changed)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QUrl referenceUrl READ referenceUrl NOTIFY changed)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY changed)
    Q_PROPERTY(QString referenceFeedback READ referenceFeedback NOTIFY changed)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    Q_PROPERTY(QString activeLayerId READ activeLayerId NOTIFY changed)
    Q_PROPERTY(bool canAddLayer READ canAddLayer NOTIFY changed)
    Q_PROPERTY(bool fillPending READ fillPending NOTIFY changed)
    Q_PROPERTY(bool fillBusy READ fillBusy NOTIFY changed)
    Q_PROPERTY(QString fillNotice READ fillNotice NOTIFY changed)
    Q_PROPERTY(QString layerNotice READ layerNotice NOTIFY changed)
    Q_PROPERTY(int eventCount READ eventCount NOTIFY changed)
    Q_PROPERTY(int replayCount READ replayCount NOTIFY changed)
    ApplicationController app_;
    WinWorkerHost host_;
    ResourceGovernor governor_;
    SteadyClock clock_;
    JobBroker broker_;
    QTimer poll_;
    std::optional<JobId> running_;
    QString workerMessage_;
    QUrl referenceUrl_;
    QString referenceFeedback_;
    QString fillNotice_;
    QString originalReferenceNotice_;
    mutable const ResourceMap* referenceCacheResources_{};
    mutable std::shared_ptr<const ResourceMap> referenceCacheOwner_;
    mutable QImage referenceCacheImage_;
    mutable std::shared_ptr<const Bytes> referenceCacheBytes_;
    mutable QString referenceCacheKey_, referenceCacheInfo_;
    void refreshOriginalReference() const;
    QThreadPool fillPool_;
    std::stop_source fillStop_;
    bool fillBusy_{false};
    std::optional<Command> pendingFill_;
    std::optional<DocumentSnapshot> pendingFillPreview_,pendingFillSource_;
    std::uint64_t pendingFillGeneration_{};
    std::optional<NodeId> activeLayer_;
    std::uint64_t canvasGeneration_{};
    WorkflowController* workflow_{};
    int replayCount_{-1};
    bool initializeCanvas(double width, double height, double ppi, bool whitePaper = false);
    void resetLayerSelection();
  public:
    Q_INVOKABLE QVariantMap sampleOriginalReference(const QString& expectedUrl,double x,double y) const;
    Q_INVOKABLE bool attachOriginalReferenceUrl(const QUrl&);
    QUrl originalReferenceUrl() const;
    QString originalReferenceInfo() const;
    QString originalReferenceNotice() const { return originalReferenceNotice_; }
    QImage originalReferenceImage(const QString& key) const;
    explicit QtBridge(QObject* parent = nullptr);
    ~QtBridge() override;
    bool fillPending() const { return pendingFill_.has_value(); }
    Q_INVOKABLE bool acceptFill();
    std::optional<DocumentSnapshot> displaySnapshot() const { return pendingFillPreview_?pendingFillPreview_:app_.snapshot(); }
    bool fillBusy() const { return fillBusy_; }
    Q_INVOKABLE void cancelFill();
    QString message() const;
    QString dimensions() const;
    bool hasDocument() const { return bool(app_.snapshot()); }
    bool dirty() const;
    bool busy() const;
    QObject* workflow() const;
    QUrl referenceUrl() const { return referenceUrl_; }
    bool hasReference() const { return !referenceUrl_.isEmpty(); }
    QString referenceFeedback() const { return referenceFeedback_; }
    QVariantList layers() const;
    QString activeLayerId() const;
    bool canAddLayer() const;
    QString layerNotice() const;
    int eventCount() const;
    int replayCount() const;
    bool replaying() const {return replayCount()<eventCount();}
    Q_INVOKABLE void setReplayCount(int);
    Q_INVOKABLE bool undoDrawing();
    Q_INVOKABLE bool redoDrawing();
    std::optional<NodeId> activeLayer() const { return activeLayer_; }
    std::uint64_t canvasGeneration() const { return canvasGeneration_; }
    std::optional<DocumentSnapshot> canvasSnapshot() const { return app_.snapshot(); }
    QString fillNotice() const { return fillNotice_; }
    Q_INVOKABLE bool importLineartUrl(const QUrl&);
    bool fillLineartAt(double x, double y, const QColor&, bool hollow = false, std::optional<QRectF> limit = std::nullopt, std::vector<DocumentPoint> polygon = {}, std::optional<QPointF> gapEnd = std::nullopt);
    bool closeLineartGapAt(double x0,double y0,double x1,double y1) { return fillLineartAt(x0,y0,Qt::black,false,std::nullopt,{},QPointF(x1,y1)); }
    Result<void> commitStroke(const StrokeEvent&);
    Q_INVOKABLE void createDocument(double width, double height, double ppi, bool whitePaper = false);
    Q_INVOKABLE bool addLayer();
    Q_INVOKABLE bool selectLayer(const QString&);
    Q_INVOKABLE bool setLayerVisible(const QString&, bool);
    Q_INVOKABLE bool importReferenceUrl(const QUrl&);
    Q_INVOKABLE void openUrl(const QUrl&);
    Q_INVOKABLE void saveUrl(const QUrl&);
    Q_INVOKABLE void probeWorker();
    Q_INVOKABLE void cancelWorker();
  signals:
    void changed();
};
}

