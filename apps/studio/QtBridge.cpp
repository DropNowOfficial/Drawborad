#include "QtBridge.h"
#include "storage/ProjectCodec.h"
#include <QCoreApplication>
#include <QUuid>
namespace drawborad {
namespace {
Id uuid() {
    const auto b = QUuid::createUuid().toRfc4122();
    Id id;
    for (int i = 0; i < 16; i++)
        id.bytes[std::size_t(i)] = std::uint8_t(b[i]);
    return id;
}
} // namespace
QtBridge::QtBridge(QObject* parent)
    : QObject(parent),
      host_(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
            L"DrawboradWorker.exe"),
      broker_(host_, governor_, clock_) {
    poll_.setInterval(25);
    connect(&poll_, &QTimer::timeout, this, [this] {
        broker_.poll();
        if (!running_)
            return;
        auto state = broker_.status(*running_);
        if (state == JobStatus::Cancelled || state == JobStatus::Failed) {
            running_.reset();
            workerMessage_ = tr("Worker stopped. Document unchanged.");
            emit changed();
            return;
        }
        auto m = host_.pollMessage(*running_);
        if (!m) {
            broker_.workerFailed(*running_);
            running_.reset();
            workerMessage_ = tr("Worker failed safely.");
            emit changed();
            return;
        }
        if (m.value()) {
            if (m.value()->kind == MessageKind::Hello) {
                workerMessage_ = tr("Native worker connected. You can move the window or cancel.");
                emit changed();
            }
            if (m.value()->kind == MessageKind::Cancelled) {
                broker_.acknowledgeCancel(m.value()->id, m.value()->nonce);
                running_.reset();
                workerMessage_ = tr("Cancelled. Document unchanged.");
                emit changed();
            }
        }
    });
    poll_.start();
}
QString QtBridge::message() const {
    return !app_.message().empty() ? QString::fromStdString(app_.message()) : workerMessage_;
}
QString QtBridge::dimensions() const {
    auto s = app_.snapshot();
    if (!s)
        return tr("No document");
    return QString::number(s->spec.pixelSize.width) + " x " +
           QString::number(s->spec.pixelSize.height) + tr(" px / revision ") +
           QString::number(s->revision);
}
void QtBridge::createDocument(double w, double h, double ppi) {
    NewDocumentRequest r;
    r.dimensions = {w, h, Unit::Px, ppi};
    auto done = app_.newDocument(r);
    (void)done;
    emit changed();
}
void QtBridge::openUrl(const QUrl& p) {
    if (!p.isLocalFile())
        return;
    auto done = app_.open(std::filesystem::path(p.toLocalFile().toStdWString()));
    (void)done;
    emit changed();
}
void QtBridge::saveUrl(const QUrl& p) {
    if (!p.isLocalFile())
        return;
    auto done = app_.save(std::filesystem::path(p.toLocalFile().toStdWString()));
    (void)done;
    emit changed();
}
void QtBridge::probeWorker() {
    if (running_ || !app_.snapshot())
        return;
    auto s = *app_.snapshot();
    auto bytes = encodeDelta(DocumentSnapshot{}, s);
    auto hash = stateHash(s);
    if (!bytes || !hash)
        return;
    JobRequest q;
    q.id = uuid();
    q.nonce = uuid();
    q.task = "native.smoke";
    q.inputSnapshot = {s.documentId, s.revision, hash.value()};
    q.snapshotBytes = std::move(bytes.value());
    q.resources = {q.id, ResourceDevice::CPU, 16 * 1024 * 1024, 0, 0, false};
    auto id = broker_.submit(q);
    if (id) {
        running_ = id.value();
        workerMessage_ = tr("Connecting native worker...");
    } else
        workerMessage_ = QString::fromStdString(id.error().messageKey);
    emit changed();
}
void QtBridge::cancelWorker() {
    if (running_) {
        broker_.cancel(*running_);
        workerMessage_ = tr("Cancelling worker...");
        emit changed();
    }
}
} // namespace drawborad
