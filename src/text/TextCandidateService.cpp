#include "TextCandidateService.h"
#include <cmath>
namespace drawborad {
Result<TextCandidates> suggestText(const TextCandidateRequest& request,
                                   const OfflineTextProvider& provider,std::stop_token stop){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"text.cancelled");
    if(!request.explicitUserRequest)
        return error(ErrorCode::ConversionConsentRequired,"text.explicit-consent-required");
    if(!request.source || !request.bounds.width || !request.bounds.height ||
       request.bounds.x<0 || request.bounds.y<0 ||
       std::uint64_t(request.bounds.x)>=request.source->width ||
       std::uint64_t(request.bounds.y)>=request.source->height ||
       request.bounds.width>request.source->width-std::uint64_t(request.bounds.x) ||
       request.bounds.height>request.source->height-std::uint64_t(request.bounds.y))
        return error(ErrorCode::InvalidInput,"text.source-or-roi");
    if(!provider)return error(ErrorCode::UnsupportedFeature,"text.local-model-unavailable");
    auto result=provider(request,stop);
    if(!result)return result.error();
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"text.cancelled");
    if(result.value().size()>128)return error(ErrorCode::BudgetExceeded,"text.candidate-budget");
    for(const auto& candidate:result.value()){
        if(candidate.suggestedText.empty() || candidate.suggestedText.size()>4096 ||
           !std::isfinite(candidate.confidence) || candidate.confidence<0 ||
           candidate.confidence>1 || candidate.language.empty())
            return error(ErrorCode::InvalidInput,"text.invalid-candidate");
    }
    TextCandidates out;
    out.suggestions=std::move(result.value());
    // Text suggestions do not alter the image, select a font, or create a PSD type layer.
    return out;
}
}
