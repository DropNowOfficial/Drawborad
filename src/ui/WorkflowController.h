#pragma once
#include "jobs/WinWorkerHost.h"
#include "review/ReviewSession.h"
#include <QObject>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <algorithm>
namespace drawborad {
class QtBridge;
class WorkflowController : public QObject {
    Q_OBJECT
    friend class QtBridge;
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool hasReview READ hasReview NOTIFY changed)
    Q_PROPERTY(bool editingExisting READ editingExisting NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl NOTIFY changed)
    Q_PROPERTY(QUrl overlayUrl READ overlayUrl NOTIFY changed)
    Q_PROPERTY(QUrl modelPackage READ modelPackage NOTIFY changed)
    Q_PROPERTY(QUrl sam3Checkpoint READ sam3Checkpoint NOTIFY changed)
    Q_PROPERTY(QString modelState READ modelState NOTIFY changed)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY changed)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY changed)
    Q_PROPERTY(int progressCompleted READ progressCompleted NOTIFY changed)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY changed)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
    Q_PROPERTY(QVariantList promptPoints READ promptPoints NOTIFY changed)
    Q_PROPERTY(QVariantList promptBox READ promptBox NOTIFY changed)
    QtBridge& bridge_;
    WinWorkerHost host_;
    ResourceGovernor governor_;
    SteadyClock clock_;
    JobBroker broker_;
    QTimer poll_;
    std::optional<JobRequest> running_;
    std::optional<Document> analysis_;
    std::optional<ReviewSession> review_;
    std::unique_ptr<QTemporaryDir> directory_;
    std::unique_ptr<QTemporaryDir> jobDirectory_;
    std::vector<QString> names_;
    std::vector<double> scores_;
    std::vector<bool> selected_;
    std::vector<NodeId> objects_;
    std::vector<Hash> baseMasks_;
    std::vector<bool> background_;
    std::vector<PromptPoint> promptPoints_;
    std::optional<PromptBox> promptBox_;
    QJsonArray reviewAudit_;
    bool appendPrompt_{};
    Revision promptBaseRevision_{};
    std::optional<Resource> sourceMetadata_;
    std::optional<DocumentId> baseDocument_;
    Revision baseRevision_{};
    std::optional<DocumentId> jobBaseDocument_;
    Revision jobBaseRevision_{};
    std::optional<DocumentSnapshot> paintBase_;
    QUrl sourceUrl_,overlayUrl_,modelPackage_,sam3Checkpoint_;
    QProcess* semanticProcess_{};
    std::unique_ptr<QTemporaryDir> semanticDirectory_;
    bool semanticBusy_{}, semanticCancelled_{};
    std::optional<DocumentId> semanticBaseDocument_;
    Revision semanticBaseRevision_{};
    QString message_;
    QString modelState_{QStringLiteral("MissingModel")};
    QString modelStatus_;
    std::optional<QJsonObject> modelReceipt_;
    int progressCompleted_{},progressTotal_{};
    int activeIndex_{-1};
    unsigned overlayVersion_{};
    bool pending_{};
    struct ReviewUiState {std::vector<QString> names;std::vector<double> scores;std::vector<bool> selected;int active{};std::vector<NodeId> objects;std::vector<Hash> masks;std::vector<bool> background;qsizetype auditSize{};};
    ReviewUiState uiState() const {return {names_,scores_,selected_,activeIndex_,objects_,baseMasks_,background_,reviewAudit_.size()};}
    std::vector<ReviewUiState> undoUi_;
    void poll();
    bool loadResult(const DocumentSnapshot&);
    bool loadPromptResult(const DocumentSnapshot&);
    bool startAnalysis(const QUrl&,bool);
    bool loadPaintResult(const DocumentSnapshot&);
    bool startPaintJob(QJsonObject);
    bool loadModelResult(const DocumentSnapshot&);
    void stopModelCheck(const QString&);
    void updateOverlay();
    void fail(const QString&);
    bool canRecordEdit();
  public:
    explicit WorkflowController(QtBridge&);
    ~WorkflowController() override;
    bool busy() const {return running_.has_value() || semanticBusy_;}
    bool analyzing() const {return semanticBusy_ || (running_ && running_->task=="image.review.sam21");}
    bool pending() const {return pending_;}
    bool hasReview() const {return review_.has_value();}
    bool editingExisting() const {return std::any_of(objects_.begin(),objects_.end(),[](NodeId id){return !id.empty();});}
    QString message() const {return message_;}
    QUrl sourceUrl() const {return sourceUrl_;}
    QUrl overlayUrl() const {return overlayUrl_;}
    QUrl modelPackage() const {return modelPackage_;}
    QUrl sam3Checkpoint() const {return sam3Checkpoint_;}
    QString modelState() const {return modelState_;}
    QString modelStatus() const {return modelStatus_;}
    bool modelReady() const {return modelReceipt_.has_value() && modelState_==QLatin1String("Ready");}
    int progressCompleted() const {return progressCompleted_;}
    int progressTotal() const {return progressTotal_;}
    QVariantList candidates() const;
    int activeIndex() const {return activeIndex_;}
    int imageWidth() const {return review_?int(review_->masks().dimensions.width):0;}
    int imageHeight() const {return review_?int(review_->masks().dimensions.height):0;}
    QVariantList promptPoints() const;
    QVariantList promptBox() const;
    Q_INVOKABLE bool addPromptPoint(double x,double y,bool positive);
    Q_INVOKABLE bool setPromptBox(double x0,double y0,double x1,double y1);
    Q_INVOKABLE void clearPrompts();
    Q_INVOKABLE bool startPrompted();
    Q_INVOKABLE bool subtractIncluded();
    Q_INVOKABLE bool addBackgroundCandidate();
    Q_INVOKABLE void setModelPackage(const QUrl&);
    Q_INVOKABLE bool checkModel();
    Q_INVOKABLE bool startImage(const QUrl&);
    Q_INVOKABLE bool importSemanticManifest(const QUrl&);
    Q_INVOKABLE bool startSam3Image(const QUrl&,const QString& promptList);
    Q_INVOKABLE void setSam3Checkpoint(const QUrl&);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool selectCandidate(int);
    Q_INVOKABLE bool includeCandidate(int,bool);
    Q_INVOKABLE bool renameCandidate(int,const QString&);
    Q_INVOKABLE bool editRectangle(bool add,double x0,double y0,double x1,double y1);
    Q_INVOKABLE bool mergeIncluded();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool accept();
    Q_INVOKABLE bool startReconstruction();
    Q_INVOKABLE bool refineRectangle(double x0,double y0,double x1,double y1);
    Q_INVOKABLE bool exportPsd(const QUrl&);
    Q_INVOKABLE bool exportNative(const QUrl&,const QString& format);
    Q_INVOKABLE bool relinkSource(const QUrl&);
    void reset();
    void restore(const DocumentSnapshot&);
  signals:
    void changed();
};
}

