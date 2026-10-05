#include "ApplicationController.h"
#include <random>
namespace drawborad {
namespace {
Id newId() {
    std::random_device r;
    Id id;
    for (auto& x : id.bytes)
        x = std::uint8_t(r());
    id.bytes[6] = (id.bytes[6] & 15) | 0x40;
    id.bytes[8] = (id.bytes[8] & 63) | 0x80;
    return id;
}
} // namespace
Result<void> ApplicationController::fail(const Error& e) {
    state_ = ApplicationState::Error;
    message_ = e.messageKey;
    return e;
}
Result<void> ApplicationController::newDocument(NewDocumentRequest request) {
    auto spec = validateDocument(request, budget_);
    if (!spec)
        return fail(spec.error());
    auto next = Document::create(spec.value(), newId());
    if (!next)
        return fail(next.error());
    document_ = std::move(next.value());
    path_.clear();
    dirty_ = true;
    state_ = ApplicationState::Ready;
    message_.clear();
    return {};
}
Result<void> ApplicationController::open(ProjectPath p) {
    auto r = store_.recover(p);
    if (!r)
        return fail(r.error());
    auto next = Document::fromSnapshot(r.value().snapshot);
    if (!next)
        return fail(next.error());
    document_ = std::move(next.value());
    path_ = std::move(p);
    dirty_ = !r.value().warnings.empty();
    state_ = dirty_ ? ApplicationState::Recovered : ApplicationState::Ready;
    message_ = dirty_ ? r.value().warnings.front() : "";
    return {};
}
Result<void> ApplicationController::save(ProjectPath p) {
    if (!document_)
        return fail(error(ErrorCode::InvalidInput, "document.none"));
    auto r = store_.save(p, document_->snapshot());
    if (!r)
        return fail(r.error());
    path_ = std::move(p);
    dirty_ = false;
    state_ = ApplicationState::Ready;
    message_.clear();
    return {};
}
Result<ChangeSet> ApplicationController::apply(const Command& c) {
    if (!document_)
        return error(ErrorCode::InvalidInput, "document.none");
    auto r = document_->apply(c, document_->snapshot().revision);
    if (r) {
        dirty_ = true;
        state_ = ApplicationState::Ready;
        message_.clear();
    } else {
        state_ = ApplicationState::Error;
        message_ = r.error().messageKey;
    }
    return r;
}
Result<ChangeSet> ApplicationController::undo() {
    if (!document_)
        return error(ErrorCode::InvalidInput, "document.none");
    auto r = document_->undo();
    if (r)
        dirty_ = true;
    return r;
}
Result<ChangeSet> ApplicationController::redo() {
    if (!document_)
        return error(ErrorCode::InvalidInput, "document.none");
    auto r = document_->redo();
    if (r)
        dirty_ = true;
    return r;
}
} // namespace drawborad
