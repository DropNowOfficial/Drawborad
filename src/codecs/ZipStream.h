#pragma once
#include "core/Types.h"
#include <functional>
#include <optional>
#include <stop_token>
namespace drawborad {
struct ZipStreamLimits {
    std::uint64_t maxBytes=2*1024*1024*1024ull;
    std::uint64_t maxMetadataBytes=4*1024*1024;
    std::uint32_t maxEntries=65535;
    std::uint32_t maxNameBytes=65535;
};
// STORE-only classic ZIP32, UTF-8 names, no encryption/ZIP64. One open entry.
// The sink is synchronous and must not re-enter the writer.
// Any error permanently poisons the writer; its partial sink must not be published.
class ZipStream {
public:
    using Sink=std::function<Result<void>(std::span<const std::uint8_t>)>;
    explicit ZipStream(Sink,ZipStreamLimits={},std::stop_token={});
    ZipStream(const ZipStream&)=delete;
    ZipStream& operator=(const ZipStream&)=delete;
    Result<void> addKnown(std::string name,std::span<const std::uint8_t>);
    Result<void> begin(std::string name);
    Result<void> write(std::span<const std::uint8_t>);
    Result<void> end();
    Result<void> finish();
    std::uint64_t bytesWritten()const{return offset_;}
private:
    struct Entry {std::string name;std::uint32_t offset{},size{},crc{};std::uint16_t flags{};};
    Sink sink_;ZipStreamLimits limits_;std::stop_token stop_;
    std::vector<Entry> entries_;Entry current_;std::uint32_t crc_=0xffffffffu;
    std::uint64_t offset_{},metadata_{};bool open_{},finished_{};
    std::optional<Error> failure_;
    Result<void> fail(Error);
    Result<void> ready();
    Result<void> emit(std::span<const std::uint8_t>);
    Result<void> start(std::string,std::uint16_t,std::uint32_t,std::uint32_t);
};
}
