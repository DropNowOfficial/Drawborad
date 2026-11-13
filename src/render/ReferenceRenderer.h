#pragma once
#include "core/Digest.h"
#include "core/Document.h"
#include "brush/CubicInkPreparation.h"
#include <map>
#include <memory>
#include <stop_token>
namespace drawborad {
inline constexpr std::uint32_t TileSide = 512;
struct TileKey { NodeId nodeId; std::int64_t tileX{}, tileY{}; std::uint8_t mip{}; auto operator<=>(const TileKey&) const = default; };
struct EventLimit { Revision revision{}; std::uint64_t lastEvent=UINT64_MAX; std::uint64_t timeTicks=UINT64_MAX; };
struct RenderStatistics { std::uint64_t indexedEvents{},visitedEvents{},rasterizedEvents{},reconstructionCalls{},sourceAssetReads{},coveragePixels{},cubicPreparations{},cubicExpandedPoints{}; };
struct PreparedEvents;
class ReferenceRenderer {
 std::shared_ptr<PreparedEvents> prepared_; RenderStatistics stats_; CubicInkPreparation cubic_;
 public:
 Result<void> prepare(const DocumentSnapshot&,std::stop_token={});
 Result<TileData> render(const DocumentSnapshot&,TileKey,EventLimit,std::stop_token={});
 Result<TileData> renderRegion(const DocumentSnapshot&,NodeId,Rect,EventLimit,std::stop_token={});
 Result<TileData> continueRegion(const DocumentSnapshot&,NodeId,Rect,EventLimit,std::uint64_t,const TileData&,std::stop_token={});
 Result<std::vector<std::size_t>> candidates(const DocumentSnapshot&,TileKey,std::stop_token={});
 RenderStatistics statistics()const{return stats_;} void resetStatistics(){stats_={};}
};
PixelHash canonicalPixelHash(const TileData&);
std::uint16_t readSample(const TileData&,std::uint64_t);
Result<Rect> strokeBounds(const StrokeEvent&);
}
