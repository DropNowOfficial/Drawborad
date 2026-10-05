#pragma once
#include "ReferenceRenderer.h"
#include <list>
#include <map>
#include <memory>
#include <set>
namespace drawborad {
struct ContentTileKey {
    TileKey tile;
    Revision contentRevision{};
    auto operator<=>(const ContentTileKey&) const = default;
};
struct DirtySet { std::set<TileKey> pigment; std::set<TileKey> display; };
class TileCache {
    struct Entry { std::shared_ptr<const TileData> tile; std::uint64_t bytes{}; std::uint32_t pins{}; std::uint64_t touch{}; };
    std::map<ContentTileKey, Entry> entries_;
    std::uint64_t maxBytes_{}, used_{}, clock_{};
  public:
    explicit TileCache(std::uint64_t maxBytes = 512 * 1024 * 1024) : maxBytes_(maxBytes) {}
    void put(ContentTileKey, std::shared_ptr<const TileData>);
    std::shared_ptr<const TileData> get(ContentTileKey);
    bool pin(ContentTileKey);
    void unpin(ContentTileKey);
    std::uint64_t trimTo(std::uint64_t bytes);
    void clear();
    std::uint64_t usedBytes() const { return used_; }
    std::size_t size() const { return entries_.size(); }
};
DirtySet affectedTiles(const ChangeSet&, const DocumentSnapshot&);
}
