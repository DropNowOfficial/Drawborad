#include "ModelManifest.h"
#include <algorithm>
#include <filesystem>
namespace drawborad {
namespace {
Result<void> copyVerifiedFile(const std::filesystem::path& sourceRoot,
                              const std::filesystem::path& relative,
                              const std::filesystem::path& destinationRoot) {
    if (relative.empty())
        return error(ErrorCode::CorruptData, "model.install-relative-path");
    std::error_code ec;
    const auto source = sourceRoot / relative;
    const auto destination = destinationRoot / relative;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec)
        return error(ErrorCode::IoError, "model.install-create-directory");
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        return error(ErrorCode::IoError, "model.install-copy");
    return {};
}
}

Result<ModelId> ModelRegistry::install(const PackagePath& p, TrustDecision trust) {
    auto verified = validatePackage(p);
    if (!verified)
        return verified.error();
    if (!verified.value().signedPackage && trust != TrustDecision::UserAcceptedUntrusted)
        return error(ErrorCode::LicenseBlocked, "model.untrusted-consent");

    ModelId id;
    std::copy_n(verified.value().sha256.bytes.begin(), 16, id.bytes.begin());
    if (installed_.contains(id))
        return id;

    auto manifest = verified.value();
    auto root = p;
    bool owned = false;

    if (!storeRoot_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(storeRoot_, ec);
        if (ec)
            return error(ErrorCode::IoError, "model.install-store-root");

        const auto packageName = manifest.sha256.hex();
        const auto finalRoot = storeRoot_ / packageName;
        const auto tempRoot = storeRoot_ / (packageName + ".tmp");

        std::filesystem::remove_all(tempRoot, ec);
        ec.clear();
        std::filesystem::create_directories(tempRoot, ec);
        if (ec)
            return error(ErrorCode::IoError, "model.install-temp-root");

        auto cleanup = [&] {
            std::error_code ignored;
            std::filesystem::remove_all(tempRoot, ignored);
        };

        auto encoder = copyVerifiedFile(p, manifest.weights, tempRoot);
        if (!encoder) {
            cleanup();
            return encoder.error();
        }
        if (!manifest.decoderWeights.empty()) {
            auto decoder = copyVerifiedFile(p, manifest.decoderWeights, tempRoot);
            if (!decoder) {
                cleanup();
                return decoder.error();
            }
        }

        std::filesystem::copy_file(
            p / "manifest.json", tempRoot / "manifest.json",
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            cleanup();
            return error(ErrorCode::IoError, "model.install-manifest-copy");
        }

        auto copied = validatePackage(tempRoot);
        if (!copied) {
            cleanup();
            return copied.error();
        }
        if (copied.value().sha256 != manifest.sha256 ||
            copied.value().decoderSha256 != manifest.decoderSha256) {
            cleanup();
            return error(ErrorCode::DigestMismatch, "model.install-postcopy-digest");
        }

        if (std::filesystem::exists(finalRoot, ec)) {
            auto existing = validatePackage(finalRoot);
            if (!existing || existing.value().sha256 != manifest.sha256 ||
                existing.value().decoderSha256 != manifest.decoderSha256) {
                cleanup();
                return error(ErrorCode::DigestMismatch, "model.install-existing-package");
            }
            cleanup();
        } else {
            ec.clear();
            std::filesystem::rename(tempRoot, finalRoot, ec);
            if (ec) {
                cleanup();
                return error(ErrorCode::IoError, "model.install-publish");
            }
        }

        auto ownedManifest = validatePackage(finalRoot);
        if (!ownedManifest)
            return ownedManifest.error();
        manifest = std::move(ownedManifest.value());
        root = finalRoot;
        owned = true;
    }

    installed_[id] = std::move(manifest);
    roots_[id] = std::move(root);
    owned_[id] = owned;
    return id;
}

Result<void> ModelRegistry::remove(ModelId id) {
    if (active_[id])
        return error(ErrorCode::Locked, "model.active");
    if (!installed_.contains(id))
        return error(ErrorCode::InvalidInput, "model.not-installed");

    if (owned_[id]) {
        std::error_code ec;
        std::filesystem::remove_all(roots_[id], ec);
        if (ec)
            return error(ErrorCode::IoError, "model.remove-owned-package");
    }

    installed_.erase(id);
    roots_.erase(id);
    active_.erase(id);
    owned_.erase(id);
    return {};
}

Result<ModelManifest> ModelRegistry::manifest(ModelId id) const {
    auto it = installed_.find(id);
    if (it == installed_.end())
        return error(ErrorCode::InvalidInput, "model.not-installed");
    return it->second;
}

Result<void> ModelRegistry::retain(ModelId id) {
    if (!installed_.contains(id))
        return error(ErrorCode::InvalidInput, "model.not-installed");
    active_[id]++;
    return {};
}

void ModelRegistry::release(ModelId id) {
    auto it = active_.find(id);
    if (it != active_.end() && it->second)
        --it->second;
}
}
