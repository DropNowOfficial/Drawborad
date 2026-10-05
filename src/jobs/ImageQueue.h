#pragma once
#include "review/ReviewSession.h"
#include <deque>
namespace drawborad {
enum class QueueState { Queued, Analyzing, WaitingForReview, Reconstructing, Paused, Cancelled, Failed, Done };
struct QueuePolicy { bool autoAcceptReview{false}; bool stopOnFailure{false}; };
struct QueueItem { Id id{}; std::filesystem::path input; QueueState state{QueueState::Queued}; Revision inputRevision{}; std::string error; };
class QueueStore;
class ImageQueue {
    friend class QueueStore;
    QueueId id_{};
    QueuePolicy policy_{};
    std::vector<QueueItem> items_;
    bool paused_{};
    std::optional<std::size_t> activeHeavy_;
  public:
    Result<QueueId> enqueue(std::span<const std::filesystem::path>,const QueuePolicy&);
    void pause(QueueId);
    Result<void> resume(QueueId);
    Result<void> startAnalysis(std::size_t);
    Result<void> analysisComplete(std::size_t,bool hasReview);
    Result<void> acceptReview(std::size_t,Revision current);
    Result<void> finish(std::size_t,bool success,std::string error={});
    Result<void> cancel(std::size_t);
    const std::vector<QueueItem>& items()const{return items_;}
    std::size_t concurrentHeavyJobs()const{return activeHeavy_?1:0;}
};
}
