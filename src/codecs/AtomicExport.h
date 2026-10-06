#pragma once
#include "CodecLimits.h"
#include "core/Digest.h"
#include "storage/FileIO.h"

namespace drawborad {

struct StagedExport {
    std::filesystem::path tempPath;
    Hash verifiedHash{};
};

struct DestinationPolicy {
    std::filesystem::path destination;
    bool replaceConfirmed{};
    bool atomic{true};
};

Result<void> commitExport(FileIO&, const StagedExport&, const DestinationPolicy&,
                          std::uint64_t maxBytes);
Result<void> commitExport(const StagedExport&, const DestinationPolicy&);

} // namespace drawborad
