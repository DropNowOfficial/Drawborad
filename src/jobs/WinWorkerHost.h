#pragma once
#include "Protocol.h"
#include <filesystem>
namespace drawborad {
class WinWorkerHost final : public WorkerHost {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit WinWorkerHost(std::filesystem::path executable);
    ~WinWorkerHost();
    Result<void> start(const JobRequest&) override;
    void requestCancel(JobId) override;
    void terminate(JobId) override;
    Result<std::optional<Message>> pollMessage(JobId);
};
int runNativeWorker(int argc, char** argv);
} // namespace drawborad
