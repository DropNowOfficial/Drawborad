#include "ProjectStore.h"
#include "Binary.h"
#include <algorithm>
namespace drawborad {
namespace fs = std::filesystem;
namespace {
fs::path suffix(fs::path p, const std::string& s) {
    p += s;
    return p;
}
Result<RecoveryResult> load(FileIO& io, const ProjectPath& p) {
    auto one = [&](const fs::path& path) -> Result<DocumentSnapshot> {
        auto b = io.read(path, MaxProjectBytes);
        if (!b)
            return b.error();
        auto r = readRecord(b.value(), 0);
        if (!r)
            return r.error();
        if (r.value().next != b.value().size())
            return error(ErrorCode::CorruptData, "project.trailing");
        return decodeDelta(r.value().data, DocumentSnapshot{});
    };
    auto s = one(p);
    std::vector<std::string> warnings;
    if (!s) {
        s = one(suffix(p, ".bak"));
        if (!s)
            return s.error();
        warnings.push_back("recovery.used-backup");
    }
    auto jp = suffix(p, ".journal");
    if (fs::exists(jp)) {
        auto b = io.read(jp, MaxProjectBytes);
        if (!b)
            return b.error();
        std::size_t pos = 0;
        while (pos < b.value().size()) {
            auto r = readRecord(b.value(), pos);
            if (!r) {
                warnings.push_back("recovery.ignored-invalid-tail");
                break;
            }
            try {
                binary::Reader peek{r.value().data};
                (void)peek.text(32);
                (void)peek.id();
                (void)peek.u64();
                (void)peek.hash();
                auto id = peek.id();
                auto revision = peek.u64();
                if (id != s.value().documentId) {
                    warnings.push_back("recovery.foreign-journal");
                    break;
                }
                if (revision > s.value().revision) {
                    auto next = decodeDelta(r.value().data, s.value());
                    if (!next) {
                        warnings.push_back("recovery.noncontinuing-tail");
                        break;
                    }
                    s = std::move(next);
                }
            } catch (const binary::Invalid&) {
                warnings.push_back("recovery.ignored-invalid-tail");
                break;
            }
            pos = r.value().next;
        }
    }
    return RecoveryResult{s.value(), s.value().revision, warnings};
}
} // namespace
Result<void> ProjectStore::save(const ProjectPath& p, const DocumentSnapshot& s) {
    auto valid = validateSnapshot(s);
    if (!valid)
        return valid.error();
    FileLease lease;
    auto held = lease.acquire(suffix(p, ".lock"));
    if (!held)
        return held.error();
    if (fs::exists(p)) {
        auto old = load(*io_, p);
        if (!old)
            return old.error();
        if (old.value().snapshot.documentId != s.documentId ||
            old.value().lastCommittedRevision > s.revision)
            return error(ErrorCode::StaleRevision, "save.stale");
        auto oldHash = stateHash(old.value().snapshot), newHash = stateHash(s);
        if (!oldHash)
            return oldHash.error();
        if (!newHash)
            return newHash.error();
        if (old.value().lastCommittedRevision == s.revision && oldHash.value() != newHash.value())
            return error(ErrorCode::StaleRevision, "save.same-revision-conflict");
        auto recovered = encodeDelta(DocumentSnapshot{}, old.value().snapshot);
        if (!recovered)
            return recovered.error();
        auto data = frameRecord(recovered.value());
        if (!data)
            return data.error();
        auto backup = atomicWrite(*io_, suffix(p, ".bak"), data.value());
        if (!backup)
            return backup.error();
    }
    auto bytes = encodeDelta(DocumentSnapshot{}, s);
    if (!bytes)
        return bytes.error();
    auto framed = frameRecord(bytes.value());
    if (!framed)
        return framed.error();
    auto saved = atomicWrite(*io_, p, framed.value());
    if (!saved)
        return saved.error();
    // Older journal records are revision-checked on recovery; a failed cleanup is safe.
    std::error_code ec;
    fs::remove(suffix(p, ".journal"), ec);
    return {};
}
Result<RecoveryResult> ProjectStore::recover(const ProjectPath& p) const {
    FileLease lease;
    auto held = lease.acquire(suffix(p, ".lock"));
    if (!held)
        return held.error();
    return load(*io_, p);
}
Result<void> ProjectStore::appendJournal(const ProjectPath& p, const DocumentSnapshot& before,
                                         const DocumentSnapshot& after) {
    FileLease lease;
    auto held = lease.acquire(suffix(p, ".lock"));
    if (!held)
        return held.error();
    auto disk = load(*io_, p);
    if (!disk)
        return disk.error();
    auto h1 = stateHash(disk.value().snapshot), h2 = stateHash(before);
    if (!h1)
        return h1.error();
    if (!h2)
        return h2.error();
    if (disk.value().lastCommittedRevision != before.revision || h1.value() != h2.value())
        return error(ErrorCode::StaleRevision, "journal.stale");
    if (!disk.value().warnings.empty())
        return error(ErrorCode::CorruptData, "journal.recover-save-before-append");
    auto delta = encodeDelta(before, after);
    if (!delta)
        return delta.error();
    auto frame = frameRecord(delta.value());
    if (!frame)
        return frame.error();
    auto jp = suffix(p, ".journal");
    Bytes existing;
    if (fs::exists(jp)) {
        auto r = io_->read(jp, MaxProjectBytes);
        if (!r)
            return r.error();
        existing = std::move(r.value());
    }
    if (existing.size() > MaxProjectBytes - frame.value().size())
        return error(ErrorCode::BudgetExceeded, "journal.compact-required");
    existing.insert(existing.end(), frame.value().begin(), frame.value().end());
    return atomicWrite(*io_, jp, existing);
}
Result<ProjectPath> ProjectStore::checkpoint(const ProjectPath& p, const DocumentSnapshot& s,
                                             bool protect) {
    auto dir = suffix(p, ".history");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return error(ErrorCode::IoError, "history.mkdir");
    auto hash = stateHash(s);
    if (!hash)
        return hash.error();
    auto dest = confinedPath(dir, std::to_string(s.revision) + "-" + hash.value().hex() + ".draw");
    if (!dest)
        return dest.error();
    auto r = save(dest.value(), s);
    if (!r)
        return r.error();
    if (protect) {
        Bytes pin{1};
        auto w = atomicWrite(*io_, suffix(dest.value(), ".pin"), pin);
        if (!w)
            return w.error();
    }
    return dest.value();
}
Result<void> ProjectStore::prune(const ProjectPath& p, Revision current, std::uint64_t budget) {
    auto dir = suffix(p, ".history");
    if (!fs::exists(dir))
        return {};
    FileLease lease;
    auto held = lease.acquire(suffix(p, ".lock"));
    if (!held)
        return held.error();
    std::vector<HistoryEntry> entries;
    try {
        for (auto& f : fs::directory_iterator(dir)) {
            if (f.path().extension() != ".draw")
                continue;
            auto dest = confinedPath(dir, f.path().filename());
            if (!dest)
                return dest.error();
            auto result = load(*io_, dest.value());
            if (!result)
                return result.error();
            auto rev = result.value().lastCommittedRevision;
            entries.push_back({dest.value(), f.file_size(), rev,
                               fs::exists(suffix(dest.value(), ".pin")), rev == current});
        }
        auto evict = historyEvictions(entries, budget);
        if (!evict)
            return evict.error();
        for (auto& e : evict.value()) {
            std::error_code ec;
            fs::remove(e, ec);
            if (ec)
                return error(ErrorCode::IoError, "history.remove");
        }
    } catch (const fs::filesystem_error&) {
        return error(ErrorCode::IoError, "history.scan");
    }
    return {};
}
} // namespace drawborad
