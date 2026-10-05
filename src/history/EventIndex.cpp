#include "EventIndex.h"
#include "render/ReferenceRenderer.h"
#include <algorithm>
namespace drawborad {
Result<void> EventIndex::build(const EventList& e){chunks_.clear();for(std::size_t i=0;i<e.size();i+=256){EventChunk c;c.first=i;c.last=std::min(e.size(),i+256);c.startUs=e[i].startUs;c.endUs=e[c.last-1].endUs;bool first=true;for(std::size_t j=c.first;j<c.last;j++){auto b=strokeBounds(e[j]);if(!b)return b.error();if(first){c.bounds=b.value();first=false;}else{auto x0=std::min(c.bounds.x,b.value().x),y0=std::min(c.bounds.y,b.value().y),x1=std::max(c.bounds.x+std::int64_t(c.bounds.width),b.value().x+std::int64_t(b.value().width)),y1=std::max(c.bounds.y+std::int64_t(c.bounds.height),b.value().y+std::int64_t(b.value().height));c.bounds={x0,y0,std::uint64_t(x1-x0),std::uint64_t(y1-y0)};}}chunks_.push_back(c);}return{};}
std::size_t EventIndex::eventCountAt(std::uint64_t us) const{std::size_t n=0;for(auto& c:chunks_){if(c.startUs>us)break;if(c.endUs<=us)n=c.last;else break;}return n;}
}
