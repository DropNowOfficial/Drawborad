#include "quality/QualityEvaluator.h"
#include <iostream>
using namespace drawborad;
namespace {
const char* label(QualityGateState state){
    switch(state){
    case QualityGateState::Passed:return "Passed";
    case QualityGateState::Failed:return "Failed";
    case QualityGateState::Blocked:return "Blocked";
    }
    return "Blocked";
}
}
int main(){
    // With no Photoshop, real ICC metrics or human process review, every gate stays blocked.
    auto report=QualityEvaluator{}.evaluate({});
    if(!report){std::cerr<<"quality_evaluator_error\n";return 1;}
    const auto decision=acceptQuality(report.value(),{});
    std::cout<<"{\"fidelity\":\""<<label(decision.fidelity)
             <<"\",\"layer_integrity\":\""<<label(decision.layers)
             <<"\",\"natural_process\":\""<<label(decision.naturalness)
             <<"\",\"accepted\":"<<(decision.accepted?"true":"false")<<"}\n";
    return decision.accepted?0:77;
}
