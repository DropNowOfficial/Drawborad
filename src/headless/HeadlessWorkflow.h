#pragma once
#include "core/Document.h"
#include "ui/SemanticManifest.h"
#include <QString>
#include <filesystem>
#include <stop_token>
namespace drawborad {
struct HeadlessWorkflowRequest {
    SemanticManifestImport input;
    std::vector<std::size_t> accepted;
    std::filesystem::path outputDirectory;
    bool multiscale{};
    QString planner{};
    bool amodalBackground{};
    PixelSize targetSize{};
    double targetDpi{300.0};
};
struct HeadlessCompilePlanRequest {
    std::filesystem::path planPath;
    std::filesystem::path outputDirectory;
    PixelSize targetSize{};
    double targetDpi{0.0};
};
struct HeadlessWorkflowReport {
    DocumentSnapshot snapshot;
    QJsonObject metadata;
};
// The caller supplies validated model output or an explicitly labelled fixture.
// No external document is mutated before the reconstruction command is complete.
Result<HeadlessWorkflowReport> runHeadlessWorkflow(const HeadlessWorkflowRequest&,std::stop_token = {});
Result<HeadlessWorkflowReport> compileHeadlessPlan(const HeadlessCompilePlanRequest&,std::stop_token = {});
}
