#pragma once
#include "core/Types.h"
#include <bit>
#include <cstring>
namespace drawborad::binary {
struct Invalid {};
struct Writer {
    Bytes data;
    void u64(std::uint64_t n) {
        for (int i = 0; i < 8; i++)
            data.push_back(std::uint8_t(n >> (i * 8)));
    }
    void real(double x) {
        u64(std::bit_cast<std::uint64_t>(x));
    }
    void boolean(bool b) {
        data.push_back(b ? 1 : 0);
    }
    void raw(std::span<const std::uint8_t> x) {
        data.insert(data.end(), x.begin(), x.end());
    }
    void id(Id x) {
        raw(x.bytes);
    }
    void hash(Hash x) {
        raw(x.bytes);
    }
    void blob(std::span<const std::uint8_t> x) {
        u64(x.size());
        raw(x);
    }
    void text(const std::string& x) {
        blob({reinterpret_cast<const std::uint8_t*>(x.data()), x.size()});
    }
};
struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t pos = 0;
    std::span<const std::uint8_t> raw(std::size_t n) {
        if (n > data.size() - pos)
            throw Invalid{};
        auto p = data.subspan(pos, n);
        pos += n;
        return p;
    }
    std::uint64_t u64() {
        auto b = raw(8);
        std::uint64_t n = 0;
        for (int i = 0; i < 8; i++)
            n |= std::uint64_t(b[i]) << (8 * i);
        return n;
    }
    std::size_t count(std::size_t maximum = 1000000) {
        auto n = u64();
        if (n > maximum || n > data.size() - pos)
            throw Invalid{};
        return std::size_t(n);
    }
    double real() {
        return std::bit_cast<double>(u64());
    }
    bool boolean() {
        auto b = raw(1)[0];
        if (b > 1)
            throw Invalid{};
        return b != 0;
    }
    Id id() {
        Id x;
        auto a = raw(16);
        std::copy(a.begin(), a.end(), x.bytes.begin());
        return x;
    }
    Hash hash() {
        Hash x;
        auto a = raw(32);
        std::copy(a.begin(), a.end(), x.bytes.begin());
        return x;
    }
    Bytes blob(std::size_t max = 64 * 1024 * 1024) {
        auto n = count(max);
        auto a = raw(n);
        return Bytes(a.begin(), a.end());
    }
    std::string text(std::size_t max = 16384) {
        auto b = blob(max);
        return std::string(b.begin(), b.end());
    }
    bool done() const {
        return pos == data.size();
    }
};
} // namespace drawborad::binary
