#pragma once
#include "core/Document.h"
#include <map>
namespace drawborad {
struct EventChunk { std::size_t first{},last{}; std::uint64_t startUs{},endUs{}; Rect bounds; };
class EventIndex { std::vector<EventChunk> chunks_; public: Result<void> build(const EventList&); std::size_t eventCountAt(std::uint64_t us) const; std::span<const EventChunk> chunks() const{return chunks_;} };
}
