#pragma once
#include "core/Digest.h"
#include "core/Result.h"
#include <map>
#include <mutex>
#include <span>
namespace drawborad {
using ProfileId = Hash;
enum class IccColorSpace { RGB, CMYK, Gray, Unknown };
struct ProfileInfo { ProfileId id{}; IccColorSpace colorSpace{IccColorSpace::Unknown}; std::uint32_t declaredSize{}; Bytes bytes; };
class ProfileStore {
 mutable std::mutex mutex_; std::map<ProfileId,ProfileInfo> profiles_;
public:
 Result<ProfileId> import(std::span<const std::uint8_t>);
 Result<ProfileInfo> get(ProfileId) const;
 bool contains(ProfileId) const; std::size_t size() const;
};
Result<ProfileInfo> inspectIcc(std::span<const std::uint8_t>);
}