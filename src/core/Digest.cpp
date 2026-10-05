#include "Digest.h"
#include <bit>
#include <cstring>
namespace drawborad {
Hash sha256(std::span<const std::uint8_t> input) {
    constexpr std::array<std::uint32_t, 64> K = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto block = [&](const std::uint8_t* p) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; i++)
            w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
                   (std::uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            auto a = w[i - 15], b = w[i - 2];
            w[i] = w[i - 16] + (std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
                   (std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10));
        }
        auto [a, b, c, d, e, f, g, z] = h;
        for (int i = 0; i < 64; i++) {
            auto t = z + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                     ((e & f) ^ (~e & g)) + K[i] + w[i];
            auto u = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
                     ((a & b) ^ (a & c) ^ (b & c));
            z = g;
            g = f;
            f = e;
            e = d + t;
            d = c;
            c = b;
            b = a;
            a = t + u;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += z;
    };
    std::size_t pos = 0;
    while (input.size() - pos >= 64) {
        block(input.data() + pos);
        pos += 64;
    }
    std::array<std::uint8_t, 128> tail{};
    const auto n = input.size() - pos;
    if (n)
        std::memcpy(tail.data(), input.data() + pos, n);
    tail[n] = 0x80;
    const std::size_t size = n < 56 ? 64 : 128;
    auto bits = std::uint64_t(input.size()) * 8;
    for (int i = 0; i < 8; i++)
        tail[size - 1 - i] = std::uint8_t(bits >> (8 * i));
    block(tail.data());
    if (size == 128)
        block(tail.data() + 64);
    Hash out;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++)
            out.bytes[i * 4 + j] = std::uint8_t(h[i] >> (24 - j * 8));
    return out;
}
} // namespace drawborad
