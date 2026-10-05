#pragma once
#include "EventIndex.h"
#include "render/ReferenceRenderer.h"
namespace drawborad {
enum class EventDirection { Backward, Forward };
struct ReplayRequest { DocumentSnapshot snapshot; TileKey tile; std::size_t eventCount{}; std::uint64_t timeUs=UINT64_MAX; };
struct ReplayFrame { TileData tile; PixelHash pixelHash; std::size_t eventCount{}; std::vector<Id> completedEventIds; };
class ReplayEngine { ReferenceRenderer renderer_; ReplayRequest last_{}; bool hasLast_{}; public: Result<ReplayFrame> seek(const ReplayRequest&,std::stop_token={}); Result<ReplayFrame> step(EventDirection,std::stop_token={}); Result<ReplayFrame> newMarks(const ReplayRequest&,std::size_t first,std::stop_token={}); RenderStatistics statistics() const{return renderer_.statistics();} };
}
