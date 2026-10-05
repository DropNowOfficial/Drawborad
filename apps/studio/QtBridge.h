#pragma once
#include "ApplicationController.h"
#include "jobs/WinWorkerHost.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
namespace drawborad {
class QtBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString dimensions READ dimensions NOTIFY changed)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    ApplicationController app_;
    WinWorkerHost host_;
    ResourceGovernor governor_;
    SteadyClock clock_;
    JobBroker broker_;
    QTimer poll_;
    std::optional<JobId> running_;
    QString workerMessage_;

  public:
    explicit QtBridge(QObject* parent = nullptr);
    QString message() const;
    QString dimensions() const;
    bool hasDocument() const {
        return bool(app_.snapshot());
    }
    bool dirty() const {
        return app_.dirty();
    }
    bool busy() const {
        return running_.has_value();
    }
    Q_INVOKABLE void createDocument(double width, double height, double ppi);
    Q_INVOKABLE void openUrl(const QUrl&);
    Q_INVOKABLE void saveUrl(const QUrl&);
    Q_INVOKABLE void probeWorker();
    Q_INVOKABLE void cancelWorker();
  signals:
    void changed();
};
} // namespace drawborad
