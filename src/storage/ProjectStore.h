#pragma once
#include "FileIO.h"
#include "ProjectCodec.h"
#include <chrono>
namespace drawborad {
struct RecoveryResult {
    DocumentSnapshot snapshot;
    Revision lastCommittedRevision{};
    std::vector<std::string> warnings;
};
struct HistoryEntry {
    ProjectPath path;
    std::uint64_t bytes{};
    Revision revision{};
    bool protectedPoint = false, current = false;
};
Result<std::vector<ProjectPath>> historyEvictions(std::span<const HistoryEntry>,
                                                  std::uint64_t budget = 10 * GiB);
class ProjectStore {
    std::shared_ptr<FileIO> io_;

  public:
    explicit ProjectStore(std::shared_ptr<FileIO> io = std::make_shared<NativeFileIO>())
        : io_(std::move(io)) {}
    Result<void> save(const ProjectPath&, const DocumentSnapshot&);
    Result<RecoveryResult> recover(const ProjectPath&) const;
    Result<void> appendJournal(const ProjectPath&, const DocumentSnapshot& before,
                               const DocumentSnapshot& after);
    Result<ProjectPath> checkpoint(const ProjectPath&, const DocumentSnapshot&, bool protect);
    Result<void> prune(const ProjectPath&, Revision current, std::uint64_t budget = 10 * GiB);
};
class Autosaver {
    std::uint64_t lastTick_{};
    Revision lastRevision_{};
    bool hasSaved_ = false;

  public:
    Result<bool> tick(std::uint64_t seconds, ProjectStore&, const ProjectPath&,
                      const DocumentSnapshot&);
};
} // namespace drawborad
