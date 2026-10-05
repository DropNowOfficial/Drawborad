#pragma once
#include "core/Commands.h"
#include "storage/ProjectStore.h"
#include <optional>
namespace drawborad {
enum class ApplicationState { Empty, Ready, Recovered, Error };
class ApplicationController {
    ResourceBudget budget_;
    ProjectStore store_;
    std::optional<Document> document_;
    ApplicationState state_ = ApplicationState::Empty;
    std::string message_;
    ProjectPath path_;
    bool dirty_ = false;
    Result<void> fail(const Error&);

  public:
    explicit ApplicationController(ResourceBudget b = {},
                                   std::shared_ptr<FileIO> io = std::make_shared<NativeFileIO>())
        : budget_(b), store_(std::move(io)) {}
    Result<void> newDocument(NewDocumentRequest);
    Result<void> open(ProjectPath);
    Result<void> save(ProjectPath);
    Result<ChangeSet> apply(const Command&);
    Result<ChangeSet> undo();
    Result<ChangeSet> redo();
    std::optional<DocumentSnapshot> snapshot() const {
        return document_ ? std::optional<DocumentSnapshot>{document_->snapshot()} : std::nullopt;
    }
    ApplicationState state() const {
        return state_;
    }
    const std::string& message() const {
        return message_;
    }
    bool dirty() const {
        return dirty_;
    }
};
} // namespace drawborad
