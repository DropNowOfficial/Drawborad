#pragma once
#include "core/Commands.h"
#include <mutex>
namespace drawborad {
inline constexpr std::uint32_t ProtocolVersion = 1;
inline constexpr std::size_t MaxControlBytes = 1024 * 1024,
                             MaxSnapshotMappingBytes = 64 * 1024 * 1024;
enum class MessageKind { Hello = 1, Progress, Result, Cancel, Cancelled, Failure };
struct Message {
    std::uint32_t version = ProtocolVersion;
    MessageKind kind = MessageKind::Hello;
    JobId id;
    Id nonce;
    Bytes payload;
};
Result<Bytes> encodeMessage(const Message&);
Result<Message> decodeMessage(std::span<const std::uint8_t>);
struct SnapshotRef {
    DocumentId projectId;
    Revision revision{};
    Hash manifestHash;
};
enum class ResourceDevice { CPU, GpuInference, GpuDisplay };
struct ResourceRequest {
    JobId id;
    ResourceDevice device = ResourceDevice::CPU;
    std::uint64_t bytes{}, ramBytes{};
    int priority{};
    bool heavy = false;
};
struct ResourceDecision {
    bool admit = false, evict = false, pause = false;
    std::string reason;
};
class ResourceGovernor {
    ResourceBudget budget_;
    std::uint64_t freeRam_, freeGpu_;
    std::map<JobId, ResourceRequest> reservations_;

  public:
    explicit ResourceGovernor(ResourceBudget b = {})
        : budget_(b), freeRam_(b.ramReclaimBytes), freeGpu_(b.gpuTotalBytes) {}
    ResourceDecision admit(const ResourceRequest&) const;
    Result<void> reserve(const ResourceRequest&);
    void release(JobId);
    void updateHeadroom(std::uint64_t ram, std::uint64_t gpu) {
        freeRam_ = ram;
        freeGpu_ = gpu;
    }
    std::size_t concurrentHeavyGpuJobs() const;
};
struct JobRequest {
    JobId id;
    SnapshotRef inputSnapshot;
    std::string task;
    std::vector<Hash> payloadRefs;
    ResourceBudget budget;
    Id nonce;
    ResourceRequest resources;
    Bytes snapshotBytes;
};
enum class JobStatus { Running, CancelRequested, Cancelled, Failed, Done };
struct JobResult {
    JobId id;
    Revision inputRevision{};
    Hash outputHash;
    JobStatus status = JobStatus::Done;
    std::vector<Resource> stagedResources;
    Id nonce;
    Bytes delta;
};
class WorkerHost {
  public:
    virtual ~WorkerHost() = default;
    virtual Result<void> start(const JobRequest&) = 0;
    virtual void requestCancel(JobId) = 0;
    virtual void terminate(JobId) = 0;
};
class Clock {
  public:
    virtual ~Clock() = default;
    virtual std::uint64_t millis() const = 0;
};
class SteadyClock : public Clock {
  public:
    std::uint64_t millis() const override;
};
class JobBroker {
    struct Job {
        JobRequest request;
        JobStatus state = JobStatus::Running;
        std::uint64_t deadline{};
    };
    WorkerHost& host_;
    ResourceGovernor& governor_;
    const Clock& clock_;
    std::map<JobId, Job> jobs_;
    mutable std::mutex mutex_;
    void finish(Job&, JobStatus);

  public:
    JobBroker(WorkerHost& host, ResourceGovernor& gov, const Clock& clock)
        : host_(host), governor_(gov), clock_(clock) {}
    Result<JobId> submit(const JobRequest&);
    void cancel(JobId);
    void poll();
    void acknowledgeCancel(JobId, Id nonce);
    void workerFailed(JobId);
    Result<ChangeSet> accept(const JobResult&, Document&);
    std::optional<JobStatus> status(JobId) const;
};
} // namespace drawborad
