#include "DisplayCache.h"
namespace drawborad {
Result<DisplayTile> DisplayCache::get(const DisplayTileKey& k) const{auto it=items_.find(k);if(it==items_.end())return error(ErrorCode::InvalidInput,"display.cache-miss");return it->second;}
void DisplayCache::invalidateProfile(Hash p){for(auto it=items_.begin();it!=items_.end();)if(it->first.displayProfile==p)it=items_.erase(it);else ++it;generation_++;}
}
