#pragma once
#include "Result.h"
#include <array>
#include <compare>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace drawborad {
using Bytes = std::vector<std::uint8_t>;
using Revision = std::uint64_t;
struct Id {
    std::array<std::uint8_t, 16> bytes{};
    auto operator<=>(const Id&) const = default;
    static Id fromNumber(std::uint64_t n) {
        Id id;
        for (int i = 0; i < 8; ++i)
            id.bytes[15 - i] = std::uint8_t(n >> (8 * i));
        return id;
    }
    bool empty() const {
        return *this == Id{};
    }
};
using NodeId = Id;
using DocumentId = Id;
using JobId = Id;
using QueueId = Id;
using ModelId = Id;
struct Hash {
    std::array<std::uint8_t, 32> bytes{};
    auto operator<=>(const Hash&) const = default;
    bool empty() const {
        return *this == Hash{};
    }
    std::string hex() const {
        const char* h = "0123456789abcdef";
        std::string s;
        for (auto b : bytes) {
            s += h[b >> 4];
            s += h[b & 15];
        }
        return s;
    }
};
using TileId = Hash;
using ProfileId = Hash;
using PixelHash = Hash;
struct PixelSize {
    std::uint64_t width{}, height{};
    bool operator==(const PixelSize&) const = default;
};
enum class Unit { Px, Mm, Cm, In };
struct Dimensions {
    double width{}, height{};
    Unit unit = Unit::Px;
    double ppi = 300;
};
inline constexpr std::uint64_t GiB = 1024ull * 1024 * 1024;
struct ResourceBudget {
    std::uint64_t ramSoftBytes = 12 * GiB, ramReclaimBytes = 18 * GiB, gpuDisplayBytes = 2 * GiB,
                  gpuTotalBytes = 6 * GiB, scratchBytes = 10 * GiB;
    std::uint32_t maxHeavyGpuJobs = 1;
};
struct Rect {
    std::int64_t x{}, y{};
    std::uint64_t width{}, height{};
    bool operator==(const Rect&) const = default;
};
struct LayerTransform {
    std::array<double, 6> affine{1, 0, 0, 1, 0, 0};
    bool operator==(const LayerTransform&) const = default;
};
Result<std::uint64_t> checkedMultiply(std::uint64_t, std::uint64_t);
Result<std::uint64_t> checkedAdd(std::uint64_t, std::uint64_t);
} // namespace drawborad
