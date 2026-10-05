#include "Protocol.h"
#include "storage/Binary.h"
#include "storage/ProjectCodec.h"
#include <chrono>
namespace drawborad {
Result<Bytes> encodeMessage(const Message& m) {
    if (m.version != ProtocolVersion || int(m.kind) < 1 || int(m.kind) > 6 || m.id.empty() ||
        m.nonce.empty() || m.payload.size() > MaxControlBytes - 128)
        return error(ErrorCode::InvalidInput, "ipc.header");
    binary::Writer w;
    w.text("DBIPC1");
    w.u64(m.version);
    w.u64(std::uint64_t(m.kind));
    w.id(m.id);
    w.id(m.nonce);
    w.blob(m.payload);
    return frameRecord(w.data);
}
Result<Message> decodeMessage(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > MaxControlBytes)
        return error(ErrorCode::BudgetExceeded, "ipc.length");
    auto f = readRecord(bytes, 0);
    if (!f)
        return f.error();
    if (f.value().next != bytes.size())
        return error(ErrorCode::CorruptData, "ipc.trailing");
    try {
        binary::Reader r{f.value().data};
        if (r.text(16) != "DBIPC1" || r.u64() != ProtocolVersion)
            throw binary::Invalid{};
        auto kind = r.u64();
        if (kind < 1 || kind > 6)
            throw binary::Invalid{};
        Message m;
        m.kind = MessageKind(kind);
        m.id = r.id();
        m.nonce = r.id();
        m.payload = r.blob(MaxControlBytes);
        if (m.id.empty() || m.nonce.empty() || !r.done())
            throw binary::Invalid{};
        return m;
    } catch (const binary::Invalid&) {
        return error(ErrorCode::CorruptData, "ipc.decode");
    }
}
std::uint64_t SteadyClock::millis() const {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
}
} // namespace drawborad
