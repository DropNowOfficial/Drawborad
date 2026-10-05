#pragma once
#include "core/Types.h"
namespace drawborad {
struct CodecLimits {
    std::uint64_t decodedBytes = 2 * GiB;
    std::uint64_t encodedBytes = 2 * GiB;
    std::uint64_t entries = 131072;
    std::uint64_t recursion = 64;
    std::uint64_t nodes = 65536;
    std::uint64_t timeoutMs = 120000;
};
inline CodecLimits defaultCodecLimits(const ResourceBudget& b) {
    CodecLimits l;
    l.decodedBytes = b.ramSoftBytes / 2;
    l.encodedBytes = b.scratchBytes / 2;
    return l;
}
}
