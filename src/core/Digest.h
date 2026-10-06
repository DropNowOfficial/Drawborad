#pragma once
#include "Types.h"
namespace drawborad {
class Sha256State {
    std::array<std::uint32_t,8> h_{
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t,64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t totalBytes_{};
    void block(const std::uint8_t*);
  public:
    void update(std::span<const std::uint8_t>);
    Hash finish() const;
};
Hash sha256(std::span<const std::uint8_t>);
}
