#pragma once

#include "core/Result.h"
#include <cstdint>

namespace drawborad {
struct AlphaReferenceCandidate {
    bool localFile{};
    bool decoded{};
    std::uint64_t encodedBytes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// Bounded preview-only reference import. It does not claim to persist the
// source image inside a .draw project or reconstruct semantic layers.
Result<void> validateAlphaReferenceCandidate(const AlphaReferenceCandidate&);
}
