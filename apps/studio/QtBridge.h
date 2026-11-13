#pragma once
#include "ApplicationController.h"
#include "jobs/WinWorkerHost.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QColor>
#include <QVariantList>
namespace drawborad {
class WorkflowController;
class QtBridge : public QObject {
    Q_OBJECT
    friend class WorkflowController;
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
    std::optional<NodeId> activeLayer_;
    std::uint64_t canvasGeneration_{};
    WorkflowController* workflow_{};
    int replayCount_{-1};
    bool initializeCanvas(double width, double height, double ppi);
    void resetLayerSelection();
  public:
    explicit QtBridge(QObject* parent = nullptr);
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
    bool fillLineartAt(double x, double y, const QColor&);
    Result<void> commitStroke(const StrokeEvent&);
    Q_INVOKABLE void createDocument(double width, double height, double ppi);
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

