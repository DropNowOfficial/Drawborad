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

Result<void> commitExport(const StagedExport& staged, const DestinationPolicy& policy) {
    if (staged.tempPath.empty() || policy.destination.empty())
        return error(ErrorCode::InvalidInput, "export.commit-path");
    if (!policy.replaceConfirmed && std::filesystem::exists(policy.destination))
        return error(ErrorCode::InvalidInput, "export.replace-consent");

    NativeFileIO io;
    auto bytes = io.read(staged.tempPath, 2 * GiB);
    if (!bytes)
        return bytes.error();
    if (sha256(bytes.value()) != staged.verifiedHash)
        return error(ErrorCode::DigestMismatch, "export.staged-hash");

    return atomicWrite(io, policy.destination, bytes.value());
}

} // namespace drawborad
