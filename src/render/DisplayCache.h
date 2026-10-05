#pragma once
#include "TileCache.h"
#include <map>
namespace drawborad {
struct DisplayTileKey {
    TileKey tile;
    Hash displayProfile;
    std::uint32_t intent{};
    std::uint32_t lod{};
    std::uint64_t transformHash{};
    Revision contentRevision{};
    auto operator<=>(const DisplayTileKey&) const = default;
};
struct DisplayTile { std::shared_ptr<const TileData> pixels; std::uint64_t generation{}; };
class DisplayCache {
    std::map<DisplayTileKey,DisplayTile> items_;
    std::uint64_t generation_{1};
  public:
    void put(DisplayTileKey k,DisplayTile t){t.generation=generation_;items_[k]=std::move(t);}
    Result<DisplayTile> get(const DisplayTileKey& k) const;
    void invalidateProfile(Hash p);
    void clear(){items_.clear();generation_++;}
    std::size_t size() const{return items_.size();}
    std::uint64_t generation() const{return generation_;}
};
}
