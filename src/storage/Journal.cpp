#include "ProjectStore.h"
#include <algorithm>
namespace drawborad {
Result<std::vector<ProjectPath>> historyEvictions(std::span<const HistoryEntry> entries,
                                                  std::uint64_t budget) {
    std::uint64_t total = 0;
    std::vector<HistoryEntry> candidates;
    for (auto& e : entries) {
        auto v = checkedAdd(total, e.bytes);
        if (!v)
            return v.error();
        total = v.value();
        if (!e.current && !e.protectedPoint)
            candidates.push_back(e);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](auto& a, auto& b) { return a.revision < b.revision; });
    std::vector<ProjectPath> evict;
    for (auto& e : candidates) {
        if (total <= budget)
            break;
        total -= e.bytes;
        evict.push_back(e.path);
    }
    if (total > budget)
        return error(ErrorCode::BudgetExceeded, "history.protected-over-budget");
    return evict;
}
Result<bool> Autosaver::tick(std::uint64_t seconds, ProjectStore& st, const ProjectPath& path,
                             const DocumentSnapshot& snapshot) {
    if (seconds < lastTick_) {
        lastTick_ = seconds;
        return false;
    }
    if (seconds - lastTick_ < 30 || (hasSaved_ && snapshot.revision == lastRevision_))
        return false;
    auto r = st.save(path, snapshot);
    if (!r)
        return r.error();
    lastTick_ = seconds;
    lastRevision_ = snapshot.revision;
    hasSaved_ = true;
    return true;
}
} // namespace drawborad
