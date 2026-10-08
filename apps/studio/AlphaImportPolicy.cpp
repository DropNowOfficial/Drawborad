#include "AlphaImportPolicy.h"

namespace drawborad {
Result<void> validateAlphaReferenceCandidate(const AlphaReferenceCandidate& candidate) {
    constexpr std::uint64_t maxEncodedBytes = 32ull * 1024ull * 1024ull;
    constexpr std::uint32_t maxDimension = 4096;
    if (!candidate.localFile)
        return error(ErrorCode::InvalidInput, "alpha.reference-local");
    if (candidate.encodedBytes == 0 || candidate.encodedBytes > maxEncodedBytes)
        return error(ErrorCode::InvalidInput, "alpha.reference-file-size");
    if (candidate.width == 0 || candidate.height == 0 ||
        candidate.width > maxDimension || candidate.height > maxDimension)
        return error(ErrorCode::InvalidInput, "alpha.reference-dimensions");
    if (!candidate.decoded)
        return error(ErrorCode::InvalidInput, "alpha.reference-decode");
    return {};
}
}
