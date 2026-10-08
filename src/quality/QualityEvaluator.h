#pragma once
#include "core/Document.h"
#include <string>
#include <vector>
namespace drawborad {
enum class QualityGateState { Passed, Failed, Blocked };
struct QualityGate {
    QualityGateState state{QualityGateState::Blocked};
    std::string reason;
};
struct MeasuredImageQuality {
    bool available{};
    bool fullResolution{};
    bool profileMatched{};
    double ssim{};
    double meanDeltaE00{};
    double p95DeltaE00{};
    double alphaMae{};
    bool criticalRoisMeasured{};
    std::vector<bool> criticalRoiAccepted;
};
struct LayerOracleEvidence {
    bool photoshopReopenVerified{};
    bool sourceFreeReplayVerified{};
    bool objectIsolationVerified{};
    bool trueEditableStrokes{};
    std::uint64_t expectedLayers{};
    std::uint64_t reopenedLayers{};
};
struct ProcessEvidence {
    bool timeOrderedReplayVerified{};
    bool sourceFallbackAbsent{};
    bool naturalProcessReviewed{};
    bool naturalProcessAccepted{};
};
struct QualityRequest {
    MeasuredImageQuality image;
    LayerOracleEvidence layers;
    ProcessEvidence process;
};
struct QualityReport {
    QualityGate fidelity;
    QualityGate layerIntegrity;
    QualityGate naturalProcess;
};
struct HumanReview {
    bool attended{};
    bool approved{};
};
struct QualityDecision {
    bool accepted{};
    QualityGateState fidelity{QualityGateState::Blocked};
    QualityGateState layers{QualityGateState::Blocked};
    QualityGateState naturalness{QualityGateState::Blocked};
};
class QualityEvaluator {
public:
    Result<QualityReport> evaluate(const QualityRequest&) const;
};
QualityDecision acceptQuality(const QualityReport&,const HumanReview&);
}
