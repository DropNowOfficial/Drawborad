#include "TileCache.h"
#include <algorithm>
namespace drawborad {
void TileCache::put(ContentTileKey k, std::shared_ptr<const TileData> t) {
    if (!t) return;
    const auto n = std::uint64_t(t->bytes.size());
    if (auto it=entries_.find(k); it!=entries_.end()) { used_-=it->second.bytes; entries_.erase(it); }
    entries_[k]={std::move(t),n,0,++clock_}; used_+=n; trimTo(maxBytes_);
}
std::shared_ptr<const TileData> TileCache::get(ContentTileKey k) {
    auto it=entries_.find(k); if(it==entries_.end()) return {}; it->second.touch=++clock_; return it->second.tile;
}
bool TileCache::pin(ContentTileKey k){auto it=entries_.find(k);if(it==entries_.end())return false;it->second.pins++;return true;}
void TileCache::unpin(ContentTileKey k){auto it=entries_.find(k);if(it!=entries_.end()&&it->second.pins)it->second.pins--;}
std::uint64_t TileCache::trimTo(std::uint64_t target){
    std::uint64_t freed=0;
    while(used_>target){auto victim=entries_.end();for(auto it=entries_.begin();it!=entries_.end();++it)if(!it->second.pins&&(victim==entries_.end()||it->second.touch<victim->second.touch))victim=it;if(victim==entries_.end())break;freed+=victim->second.bytes;used_-=victim->second.bytes;entries_.erase(victim);}return freed;
}
void TileCache::clear(){entries_.clear();used_=0;}
DirtySet affectedTiles(const ChangeSet& c,const DocumentSnapshot& s){
    DirtySet out;std::vector<NodeId> nodes=c.changedNodes;if(nodes.empty())return out;
    for(const auto& r:c.dirtyRegions){if(!r.width||!r.height)continue;auto x0=std::max<std::int64_t>(0,r.x)/TileSide, y0=std::max<std::int64_t>(0,r.y)/TileSide;auto ex=std::min<std::uint64_t>(s.spec.pixelSize.width,std::uint64_t(std::max<std::int64_t>(0,r.x))+r.width);auto ey=std::min<std::uint64_t>(s.spec.pixelSize.height,std::uint64_t(std::max<std::int64_t>(0,r.y))+r.height);if(!ex||!ey)continue;auto x1=(ex-1)/TileSide,y1=(ey-1)/TileSide;for(auto n:nodes)for(auto y=std::uint64_t(y0);y<=y1;y++)for(auto x=std::uint64_t(x0);x<=x1;x++){TileKey k{n,std::int64_t(x),std::int64_t(y),0};out.pigment.insert(k);out.display.insert(k);}}
    if(c.dirtyRegions.empty()) for(auto n:nodes){auto it=s.nodes->find(n);if(it!=s.nodes->end()){auto b=it->second.bounds;if(!b.width||!b.height)b={0,0,s.spec.pixelSize.width,s.spec.pixelSize.height};ChangeSet cc=c;cc.dirtyRegions={b};auto d=affectedTiles(cc,s);out.display.insert(d.display.begin(),d.display.end());}}
    return out;
}
}
