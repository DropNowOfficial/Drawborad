#pragma once
#include "core/Result.h"
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>
namespace drawborad {
struct Rgba8 {
    std::uint8_t r{}, g{}, b{}, a{};
    bool operator==(const Rgba8&) const = default;
};
struct FillRun {
    std::uint32_t y{}, x{}, length{};
    bool operator==(const FillRun&) const = default;
};
struct FillBudget {
    std::uint64_t maxPixels = 33554432;
    std::uint64_t maxRuns = 65536;
};
struct FillMask {
    std::vector<FillRun> runs;
    std::uint64_t pixelCount{};
};
// Read-only 4-connected selection. Tolerance is per channel against the seed,
// not the previous pixel. Fully transparent RGB values are equivalent.
Result<FillMask> floodFill(std::span<const Rgba8> pixels, std::uint32_t width,
                          std::uint32_t height, std::uint32_t seedX,
                          std::uint32_t seedY, std::uint8_t tolerance,
                          FillBudget budget = {}, std::stop_token stop = {});
}
