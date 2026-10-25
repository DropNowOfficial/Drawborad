#pragma once
#include "ReviewJob.h"
#include "core/Commands.h"
namespace drawborad {
inline constexpr auto ReconstructionRequestType="drawborad.reconstruct-request.v1";
inline constexpr auto ReconstructionResultType="drawborad.reconstruct-result.v1";
Result<Bytes> runReconstructionJob(const DocumentSnapshot&,std::stop_token = {},AnalysisProgress = {});
Result<Command> reconstructionCommand(const DocumentSnapshot& before,const DocumentSnapshot& after);
}
