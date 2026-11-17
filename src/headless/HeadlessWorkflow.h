#pragma once
#include "core/Document.h"
#include "ui/SemanticManifest.h"
#include <filesystem>
#include <stop_token>
namespace drawborad {
struct HeadlessWorkflowRequest {
    SemanticManifestImport input;
    std::vector<std::size_t> accepted;
    std::filesystem::path outputDirectory;
    bool multiscale{};
};
struct HeadlessWorkflowReport {
    DocumentSnapshot snapshot;
    QJsonObject metadata;
};
// The caller supplies validated model output or an explicitly labelled fixture.
// No external document is mutated before the reconstruction command is complete.
Result<HeadlessWorkflowReport> runHeadlessWorkflow(const HeadlessWorkflowRequest&,std::stop_token = {});
}
