#pragma once
#include "core/Digest.h"
#include "core/Document.h"
namespace drawborad {
inline constexpr std::uint64_t MaxProjectBytes = 512ull * 1024 * 1024;
Result<Bytes> encodeDelta(const DocumentSnapshot& before, const DocumentSnapshot& after);
Result<DocumentSnapshot> decodeDelta(std::span<const std::uint8_t>, const DocumentSnapshot& base);
Result<Hash> stateHash(const DocumentSnapshot&);
Result<Bytes> frameRecord(std::span<const std::uint8_t>);
struct Record {
    Bytes data;
    std::size_t next{};
};
Result<Record> readRecord(std::span<const std::uint8_t>, std::size_t offset);
} // namespace drawborad
