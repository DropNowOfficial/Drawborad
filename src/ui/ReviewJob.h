#pragma once
#include "core/Document.h"
#include "segmentation/ProposalEngine.h"
#include <QJsonObject>
namespace drawborad {
inline constexpr auto ReviewRequestType="drawborad.review-request.v1";
inline constexpr auto ReviewResultType="drawborad.review-result.v1";
inline constexpr auto SourceReferenceType="drawborad.source-reference.v1";
Resource jsonResource(const QJsonObject&);
std::optional<QJsonObject> findJsonResource(const DocumentSnapshot&, const char* type);
Result<QJsonObject> jobRequestMetadata(const DocumentSnapshot&,const char* type);
Result<std::optional<PromptSet>> reviewPromptRequest(const QJsonObject&,PixelSize);
Result<Bytes> runImageReviewJob(const DocumentSnapshot&, std::stop_token, AnalysisProgress = {});
}
