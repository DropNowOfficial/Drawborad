#include "Protocol.h"
#include "storage/ProjectCodec.h"
namespace drawborad {
Result<JobId> JobBroker::submit(const JobRequest& r) {
    std::lock_guard lock(mutex_);
    if (r.id.empty() || r.nonce.empty() || r.id != r.resources.id ||
        r.inputSnapshot.projectId.empty() || r.inputSnapshot.manifestHash.empty() ||
        r.task.empty() || r.task.size() > 128 || r.snapshotBytes.size() > MaxSnapshotMappingBytes ||
        jobs_.contains(r.id))
        return error(ErrorCode::InvalidInput, "job.request");
    auto input = decodeDelta(r.snapshotBytes, DocumentSnapshot{});
    if (!input)
        return input.error();
    auto h = stateHash(input.value());
    if (!h)
        return h.error();
    if (input.value().documentId != r.inputSnapshot.projectId ||
        input.value().revision != r.inputSnapshot.revision ||
        h.value() != r.inputSnapshot.manifestHash)
        return error(ErrorCode::DigestMismatch, "job.snapshot");
    if (jobs_.size() >= 256) {
        for (auto it = jobs_.begin(); it != jobs_.end() && jobs_.size() >= 256;) {
            if (it->second.state != JobStatus::Running &&
                it->second.state != JobStatus::CancelRequested)
                it = jobs_.erase(it);
            else
                ++it;
        }
        if (jobs_.size() >= 256)
            return error(ErrorCode::BudgetExceeded, "job.history-capacity");
    }
    auto res = governor_.reserve(r.resources);
    if (!res)
        return res.error();
    auto started = host_.start(r);
    if (!started) {
        governor_.release(r.id);
        return started.error();
    }
    jobs_.emplace(r.id, Job{r, JobStatus::Running, 0});
    return r.id;
}
void JobBroker::finish(Job& j, JobStatus s) {
    j.state = s;
    governor_.release(j.request.id);
    host_.terminate(j.request.id);
    j.request.snapshotBytes.clear();
    j.request.snapshotBytes.shrink_to_fit();
}
void JobBroker::cancel(JobId id) {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end() || it->second.state != JobStatus::Running)
        return;
    auto& j = it->second;
    j.state = JobStatus::CancelRequested;
    auto t = clock_.millis();
    j.deadline = t > UINT64_MAX - 5000 ? UINT64_MAX : t + 5000;
    host_.requestCancel(id);
}
void JobBroker::poll() {
    std::lock_guard lock(mutex_);
    for (auto& [id, j] : jobs_) {
        (void)id;
        if (j.state == JobStatus::CancelRequested && clock_.millis() >= j.deadline)
            finish(j, JobStatus::Cancelled);
    }
}
void JobBroker::acknowledgeCancel(JobId id, Id nonce) {
    std::lock_guard lock(mutex_);
    auto i = jobs_.find(id);
    if (i != jobs_.end() && i->second.state == JobStatus::CancelRequested &&
        i->second.request.nonce == nonce)
        finish(i->second, JobStatus::Cancelled);
}
void JobBroker::workerFailed(JobId id) {
    std::lock_guard lock(mutex_);
    auto i = jobs_.find(id);
    if (i != jobs_.end() &&
        (i->second.state == JobStatus::Running || i->second.state == JobStatus::CancelRequested))
        finish(i->second, JobStatus::Failed);
}
std::optional<JobStatus> JobBroker::status(JobId id) const {
    std::lock_guard lock(mutex_);
    auto i = jobs_.find(id);
    return i == jobs_.end() ? std::nullopt : std::optional<JobStatus>{i->second.state};
}
Result<ChangeSet> JobBroker::accept(const JobResult& result, Document& document) {
    std::lock_guard lock(mutex_);
    auto it = jobs_.find(result.id);
    if (it == jobs_.end())
        return error(ErrorCode::InvalidInput, "job.unknown");
    auto& j = it->second;
    if (result.nonce != j.request.nonce)
        return error(ErrorCode::InvalidInput, "job.nonce");
    if (j.state == JobStatus::CancelRequested || j.state == JobStatus::Cancelled)
        return error(ErrorCode::Cancelled, "job.cancelled");
    if (j.state != JobStatus::Running)
        return error(ErrorCode::InvalidInput, "job.duplicate-or-terminal");
    auto s = document.snapshot();
    if (result.inputRevision != j.request.inputSnapshot.revision ||
        s.revision != result.inputRevision || s.documentId != j.request.inputSnapshot.projectId) {
        finish(j, JobStatus::Failed);
        return error(ErrorCode::StaleRevision, "job.stale");
    }
    auto reject = [&](Error e) -> Result<ChangeSet> {
        finish(j, JobStatus::Failed);
        return e;
    };
    auto h = stateHash(s);
    if (!h)
        return reject(h.error());
    if (h.value() != j.request.inputSnapshot.manifestHash)
        return reject(error(ErrorCode::DigestMismatch, "job.base"));
    if (result.status != JobStatus::Done)
        return reject(error(ErrorCode::InvalidInput, "job.not-complete"));
    if (result.delta.size() > MaxSnapshotMappingBytes || sha256(result.delta) != result.outputHash)
        return reject(error(ErrorCode::DigestMismatch, "job.output"));
    if (!result.stagedResources.empty())
        return reject(error(ErrorCode::InvalidInput, "job.resources-must-be-in-hashed-delta"));
    auto next = decodeDelta(result.delta, s);
    if (!next)
        return reject(next.error());
    auto commit = document.commitExternal(next.value(), s.revision);
    if (!commit)
        return reject(commit.error());
    finish(j, JobStatus::Done);
    return commit;
}
} // namespace drawborad
