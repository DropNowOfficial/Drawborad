#include "AtomicExport.h"

namespace drawborad {

Result<void> commitExport(FileIO& io, const StagedExport& staged,
                          const DestinationPolicy& policy, std::uint64_t maxBytes) {
    if (staged.tempPath.empty() || policy.destination.empty() || maxBytes == 0)
        return error(ErrorCode::InvalidInput, "export.commit-path");
    if (!policy.atomic)
        return error(ErrorCode::UnsupportedFeature, "export.non-atomic-policy");

    const auto stagedParent = staged.tempPath.lexically_normal().parent_path();
    const auto destinationParent = policy.destination.lexically_normal().parent_path();
    if (stagedParent != destinationParent)
        return error(ErrorCode::InvalidInput, "export.staging-must-share-directory");

    if (!policy.replaceConfirmed && std::filesystem::exists(policy.destination))
        return error(ErrorCode::InvalidInput, "export.replace-consent");

    auto flushed = io.flush(staged.tempPath);
    if (!flushed)
        return flushed.error();

    auto digest = io.hash(staged.tempPath, maxBytes);
    if (!digest)
        return digest.error();
    if (digest.value() != staged.verifiedHash)
        return error(ErrorCode::DigestMismatch, "export.staged-hash");

    return io.publish(staged.tempPath, policy.destination, policy.replaceConfirmed);
}

Result<void> commitExport(const StagedExport& staged, const DestinationPolicy& policy) {
    NativeFileIO io;
    return commitExport(io, staged, policy, 2 * GiB);
}

} // namespace drawborad
