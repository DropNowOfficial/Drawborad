#include "QueueStore.h"
#include "storage/Binary.h"
#include <system_error>

namespace drawborad {
namespace {
std::string pathText(const std::filesystem::path& p) {
    auto u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
std::filesystem::path textPath(const std::string& s) {
    auto first = reinterpret_cast<const char8_t*>(s.data());
    return std::filesystem::path(std::u8string(first, first + s.size()));
}
QueueState recoverState(QueueState state, Revision revision) {
    switch (state) {
    case QueueState::Analyzing:
        return QueueState::Queued;
    case QueueState::Reconstructing:
        return revision ? QueueState::WaitingForReview : QueueState::Queued;
    case QueueState::Paused:
        return QueueState::Queued;
    default:
        return state;
    }
}
}

Result<void> QueueStore::save(const ImageQueue& q, const std::filesystem::path& path) {
    if (path.empty() || q.id_.empty())
        return error(ErrorCode::InvalidInput, "queue.store-path-or-id");
    binary::Writer w;
    w.text("DRAWBORAD-QUEUE-1");
    w.id(q.id_);
    w.boolean(q.policy_.autoAcceptReview);
    w.boolean(q.policy_.stopOnFailure);
    w.boolean(q.paused_);
    w.u64(q.items_.size());
    for (const auto& item : q.items_) {
        w.id(item.id);
        w.text(pathText(item.input));
        w.u64(std::uint64_t(item.state));
        w.u64(item.inputRevision);
        w.text(item.error);
    }
    if (w.data.size() > 16 * 1024 * 1024)
        return error(ErrorCode::BudgetExceeded, "queue.store-size");
    NativeFileIO io;
    return atomicWrite(io, path, w.data);
}

Result<ImageQueue> QueueStore::load(const std::filesystem::path& path) {
    if (path.empty())
        return error(ErrorCode::InvalidInput, "queue.store-path");
    NativeFileIO io;
    auto data = io.read(path, 16 * 1024 * 1024);
    if (!data)
        return data.error();
    try {
        binary::Reader r{data.value()};
        if (r.text(64) != "DRAWBORAD-QUEUE-1")
            return error(ErrorCode::CorruptData, "queue.store-magic");
        ImageQueue q;
        q.id_ = r.id();
        if (q.id_.empty())
            return error(ErrorCode::CorruptData, "queue.store-id");
        q.policy_.autoAcceptReview = r.boolean();
        q.policy_.stopOnFailure = r.boolean();
        q.paused_ = r.boolean();
        auto count = r.count(100000);
        q.items_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            QueueItem item;
            item.id = r.id();
            item.input = textPath(r.text(32768));
            auto state = r.u64();
            if (state > std::uint64_t(QueueState::Done))
                return error(ErrorCode::CorruptData, "queue.store-state");
            item.inputRevision = r.u64();
            item.error = r.text(16384);
            if (item.id.empty() || item.input.empty())
                return error(ErrorCode::CorruptData, "queue.store-item");
            item.state = recoverState(QueueState(state), item.inputRevision);
            q.items_.push_back(std::move(item));
        }
        if (!r.done())
            return error(ErrorCode::CorruptData, "queue.store-trailing-data");
        q.activeHeavy_.reset();
        return q;
    } catch (const binary::Invalid&) {
        return error(ErrorCode::CorruptData, "queue.store-decode");
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "queue.store-allocation");
    }
}
}
