#pragma once
#include "reconstruct/CriticalRegions.h"
#include <functional>

namespace drawborad {
struct TextCandidate {
    std::string suggestedText;
    double confidence{};
    std::string language;
};
struct TextCandidates {
    std::vector<TextCandidate> suggestions;
    bool sourceModified{};
    bool editableTextLayerCreated{};
    bool fontSubstituted{};
};
struct TextCandidateRequest {
    bool explicitUserRequest{};
    Rect bounds{};
    const TileData* source{};
};
using OfflineTextProvider =
    std::function<Result<std::vector<TextCandidate>>(const TextCandidateRequest&, std::stop_token)>;
Result<TextCandidates> suggestText(const TextCandidateRequest&, const OfflineTextProvider& = {},
                                   std::stop_token = {});
}
