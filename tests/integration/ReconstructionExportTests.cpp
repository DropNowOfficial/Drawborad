#include "Test.h"
#include "quality/QualityEvaluator.h"
using namespace drawborad;
namespace {
QualityRequest goodEvidence(){
    QualityRequest r;
    r.image={true,true,true,.991,1.5,3.0,.002,true,{true,true,true}};
    r.layers={true,true,true,true,9,9};
    r.process={true,true,true,true};
    return r;
}
}
TEST("E06 absence of machine Photoshop and human evidence stays blocked") {
    auto report=QualityEvaluator{}.evaluate({});CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Blocked);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Blocked);
    CHECK(report.value().naturalProcess.state==QualityGateState::Blocked);
    CHECK(!acceptQuality(report.value(),{}).accepted);
}
TEST("E06 high global SSIM does not conceal one wrong critical text ROI") {
    auto input=goodEvidence();input.image.criticalRoiAccepted[1]=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Failed);
    CHECK(!acceptQuality(report.value(),{true,true}).accepted);
}
TEST("E06 photo-perfect rendering but unnatural drawing process is rejected") {
    auto input=goodEvidence();input.process.naturalProcessAccepted=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Passed);
    CHECK(report.value().naturalProcess.state==QualityGateState::Failed);
    CHECK(!acceptQuality(report.value(),{true,true}).accepted);
}
TEST("E06 missing Photoshop roundtrip cannot be made green by local test metrics") {
    auto input=goodEvidence();input.layers.photoshopReopenVerified=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Blocked);
    CHECK(!acceptQuality(report.value(),{true,true}).accepted);
}
TEST("E06 reopened PSD layer count mismatch is an independent hard failure") {
    auto input=goodEvidence();input.layers.reopenedLayers=8;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Failed);
    CHECK(!acceptQuality(report.value(),{true,true}).accepted);
}
TEST("E06 missing human process review stays blocked even when metrics pass") {
    auto input=goodEvidence();input.process.naturalProcessReviewed=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().naturalProcess.state==QualityGateState::Blocked);
    CHECK(!acceptQuality(report.value(),{true,true}).accepted);
}
TEST("E06 alpha gate is independent of SSIM and color Delta E") {
    auto input=goodEvidence();input.image.alphaMae=0.1;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Failed);
}
TEST("E06 non-finite or contradictory metrics return a real error") {
    auto input=goodEvidence();input.image.ssim=2.0;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(!report);
    input=goodEvidence();input.image.p95DeltaE00=0.25;
    report=QualityEvaluator{}.evaluate(input);CHECK(!report);
}
TEST("E06 missing full resolution or color profile blocks image gate") {
    auto input=goodEvidence();input.image.fullResolution=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Blocked);
    input=goodEvidence();input.image.profileMatched=false;
    report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Blocked);
}
TEST("E06 no true strokes or source-independent replay fails layer integrity") {
    auto input=goodEvidence();input.layers.trueEditableStrokes=false;
    auto report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Failed);
    input=goodEvidence();input.layers.sourceFreeReplayVerified=false;
    report=QualityEvaluator{}.evaluate(input);CHECK(report);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Failed);
}
TEST("E06 explicit human acceptance is mandatory even after three conditional passes") {
    auto report=QualityEvaluator{}.evaluate(goodEvidence());CHECK(report);
    CHECK(report.value().fidelity.state==QualityGateState::Passed);
    CHECK(report.value().layerIntegrity.state==QualityGateState::Passed);
    CHECK(report.value().naturalProcess.state==QualityGateState::Passed);
    CHECK(!acceptQuality(report.value(),{}).accepted);
    CHECK(!acceptQuality(report.value(),{true,false}).accepted);
    CHECK(acceptQuality(report.value(),{true,true}).accepted);
}
