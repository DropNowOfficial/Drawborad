#include "QualityEvaluator.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
Result<QualityReport> QualityEvaluator::evaluate(const QualityRequest& request) const {
    const auto& image=request.image;
    if(image.available && (!std::isfinite(image.ssim) || image.ssim<0 || image.ssim>1 ||
       !std::isfinite(image.meanDeltaE00) || image.meanDeltaE00<0 ||
       !std::isfinite(image.p95DeltaE00) || image.p95DeltaE00<image.meanDeltaE00 ||
       !std::isfinite(image.alphaMae) || image.alphaMae<0 || image.alphaMae>1))
        return error(ErrorCode::InvalidInput,"quality.invalid-metrics");
    QualityReport report;
    if(!image.available || !image.fullResolution || !image.profileMatched ||
       !image.criticalRoisMeasured || image.criticalRoiAccepted.empty()) {
        report.fidelity={QualityGateState::Blocked,"quality.image-or-critical-evidence-missing"};
    }else if(image.ssim<.98 || image.meanDeltaE00>2.0 ||
             image.p95DeltaE00>5.0 || image.alphaMae>2.0/255.0 ||
             std::any_of(image.criticalRoiAccepted.begin(),image.criticalRoiAccepted.end(),
                         [](bool passed){return !passed;})) {
        report.fidelity={QualityGateState::Failed,"quality.fidelity-metrics-or-roi-failed"};
    }else{
        report.fidelity={QualityGateState::Passed,"quality.fidelity-metric-conditions-met"};
    }
    const auto& layers=request.layers;
    if(!layers.photoshopReopenVerified){
        report.layerIntegrity={QualityGateState::Blocked,"quality.photoshop-oracle-missing"};
    }else if(!layers.sourceFreeReplayVerified || !layers.objectIsolationVerified ||
             !layers.trueEditableStrokes || layers.expectedLayers==0 ||
             layers.expectedLayers!=layers.reopenedLayers){
        report.layerIntegrity={QualityGateState::Failed,"quality.layer-integrity-failed"};
    }else{
        report.layerIntegrity={QualityGateState::Passed,"quality.layer-conditions-met"};
    }
    const auto& process=request.process;
    if(!process.naturalProcessReviewed){
        report.naturalProcess={QualityGateState::Blocked,"quality.human-process-oracle-missing"};
    }else if(!process.naturalProcessAccepted || !process.timeOrderedReplayVerified ||
             !process.sourceFallbackAbsent){
        report.naturalProcess={QualityGateState::Failed,"quality.process-invalid"};
    }else{
        report.naturalProcess={QualityGateState::Passed,"quality.process-conditions-met"};
    }
    return report;
}
QualityDecision acceptQuality(const QualityReport& report,const HumanReview& review){
    QualityDecision decision;
    decision.fidelity=report.fidelity.state;
    decision.layers=report.layerIntegrity.state;
    decision.naturalness=report.naturalProcess.state;
    decision.accepted=review.attended && review.approved &&
        decision.fidelity==QualityGateState::Passed &&
        decision.layers==QualityGateState::Passed &&
        decision.naturalness==QualityGateState::Passed;
    return decision;
}
} // namespace drawborad
