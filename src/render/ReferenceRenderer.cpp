#include "ReferenceRenderer.h"
#include "CoverageRasterizer.h"
#include "core/ArtistOptIn.h"
#include <algorithm>
namespace drawborad {
struct PreparedEvents { std::shared_ptr<const EventList> events; ColorSpec color; std::map<TileKey,std::vector<std::size_t>> tiles; std::vector<Rect> bounds; std::shared_ptr<const PreparedCubicEvents> cubics; };
namespace {
bool intersects(Rect a,Rect b){return a.x<std::int64_t(b.x+std::int64_t(b.width))&&b.x<a.x+std::int64_t(a.width)&&a.y<std::int64_t(b.y+std::int64_t(b.height))&&b.y<a.y+std::int64_t(a.height);}
std::int64_t tileAt(std::int64_t p){return p>=0?p/TileSide:-((-p+TileSide-1)/TileSide);}
bool clipsToRoi(const StrokeEvent& e){
 return (e.engineVersion==2 && (e.toolId=="marker" || e.toolId=="highlight")) ||
        e.engineVersion==10 || e.engineVersion==12 || e.engineVersion==19 ||
        e.engineVersion==21 || e.engineVersion==22 || hasLinearOpticalBlend(e);
}
Result<Rect> tileRect(const DocumentSnapshot&s,TileKey k){
 if(k.mip)return error(ErrorCode::UnsupportedFeature,"render.pigment-mip");
 if(k.tileX<0||k.tileY<0)return error(ErrorCode::InvalidInput,"render.tile-outside");
 auto x=std::uint64_t(k.tileX)*TileSide,y=std::uint64_t(k.tileY)*TileSide;if(x>=s.spec.pixelSize.width||y>=s.spec.pixelSize.height)return error(ErrorCode::InvalidInput,"render.tile-outside");
 return Rect{std::int64_t(x),std::int64_t(y),std::min<std::uint64_t>(TileSide,s.spec.pixelSize.width-x),std::min<std::uint64_t>(TileSide,s.spec.pixelSize.height-y)};
}}
Result<void> ReferenceRenderer::prepare(const DocumentSnapshot&s,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"render.cancelled");
 if(prepared_&&prepared_->events==s.eventIndex&&prepared_->color==s.spec.colorSpec){
  if(!prepared_->cubics)return{}; // Legacy path retains the immutable-snapshot contract.
  auto current=cubic_.prepare(s.eventIndex,stop);if(!current)return current.error();
  if(current.value()==prepared_->cubics)return{};
 }
 auto valid=validateSnapshot(s);
 if(!valid)return valid.error();
 auto p=std::make_shared<PreparedEvents>();p->events=s.eventIndex;p->color=s.spec.colorSpec;p->bounds.reserve(s.eventIndex->size());
 if(std::any_of(s.eventIndex->begin(),s.eventIndex->end(),[](const auto&e){return isAuthoredCubic(e);})){auto c=cubic_.prepare(s.eventIndex,stop);if(!c)return c.error();p->cubics=c.value();stats_.cubicPreparations++;stats_.cubicExpandedPoints+=c.value()->expandedPoints;}else cubic_=CubicInkPreparation{};
 for(std::size_t i=0;i<s.eventIndex->size();i++){if(stop.stop_requested())return error(ErrorCode::Cancelled,"render.cancelled");auto b=strokeBounds(s.eventIndex->at(i));if(!b)return b.error();p->bounds.push_back(b.value());auto x0=tileAt(b.value().x),y0=tileAt(b.value().y),x1=tileAt(b.value().x+std::int64_t(b.value().width)-1),y1=tileAt(b.value().y+std::int64_t(b.value().height)-1);if((x1-x0+1)*(y1-y0+1)>4096)return error(ErrorCode::BudgetExceeded,"render.bounds");for(auto y=y0;y<=y1;y++)for(auto x=x0;x<=x1;x++)p->tiles[{s.eventIndex->at(i).layerId,x,y,0}].push_back(i);stats_.indexedEvents++;}
 prepared_=std::move(p);return{};
}
Result<std::vector<std::size_t>> ReferenceRenderer::candidates(const DocumentSnapshot&s,TileKey k,std::stop_token stop){auto p=prepare(s,stop);if(!p)return p.error();if(auto it=prepared_->tiles.find(k);it!=prepared_->tiles.end())return it->second;return std::vector<std::size_t>{};}
Result<TileData> ReferenceRenderer::render(const DocumentSnapshot&s,TileKey k,EventLimit l,std::stop_token stop){if(stop.stop_requested())return error(ErrorCode::Cancelled,"render.cancelled");auto r=tileRect(s,k);if(!r)return r.error();return renderRegion(s,k.nodeId,r.value(),l,stop);}
Result<TileData> ReferenceRenderer::renderRegion(const DocumentSnapshot&s,NodeId id,Rect rect,EventLimit l,std::stop_token stop){TileData empty;return continueRegion(s,id,rect,l,0,empty,stop);}
Result<TileData> ReferenceRenderer::continueRegion(const DocumentSnapshot&s,NodeId id,Rect rect,EventLimit l,std::uint64_t first,const TileData& checkpoint,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"render.cancelled");
 if(l.revision!=s.revision)return error(ErrorCode::StaleRevision,"render.revision");
 if(!rect.width||!rect.height||rect.width>TileSide||rect.height>TileSide||rect.x<0||rect.y<0||std::uint64_t(rect.x)+rect.width>s.spec.pixelSize.width||std::uint64_t(rect.y)+rect.height>s.spec.pixelSize.height)return error(ErrorCode::InvalidInput,"render.region");
 auto nit=s.nodes->find(id);if(nit==s.nodes->end()||nit->second.kind!=NodeKind::Layer)return error(ErrorCode::InvalidInput,"render.layer");if(nit->second.sourceKind!=SourceKind::Reconstructed||!nit->second.content.empty())return error(ErrorCode::UnsupportedFeature,"render.imported-content-needs-codec");if(!s.spec.colorSpec.format.alpha)return error(ErrorCode::UnsupportedFeature,"render.layer-requires-alpha");
 auto ok=prepare(s,stop);if(!ok)return ok.error();TileData t;t.width=std::uint32_t(rect.width);t.height=std::uint32_t(rect.height);t.format=s.spec.colorSpec.format;t.profileId=s.spec.colorSpec.profileId;t.rowStride=rect.width*t.format.channelCount()*(t.format.depth==Depth::U16?2:1);try{t.bytes.resize(std::size_t(t.rowStride*t.height));}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"render.tile-allocation");}
 if(first){if(checkpoint.width!=t.width||checkpoint.height!=t.height||checkpoint.rowStride!=t.rowStride||checkpoint.format!=t.format||checkpoint.profileId!=t.profileId||checkpoint.bytes.size()!=t.bytes.size())return error(ErrorCode::CorruptData,"render.checkpoint-layout");t.bytes=checkpoint.bytes;}
 std::vector<std::size_t> ids;for(auto y=tileAt(rect.y);y<=tileAt(rect.y+std::int64_t(rect.height)-1);y++)for(auto x=tileAt(rect.x);x<=tileAt(rect.x+std::int64_t(rect.width)-1);x++){auto c=candidates(s,{id,x,y,0},stop);if(!c)return c.error();ids.insert(ids.end(),c.value().begin(),c.value().end());}std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());stats_.reconstructionCalls++;
 for(auto i:ids){if(i<first||i>=l.lastEvent)continue;stats_.visitedEvents++;const auto&e=s.eventIndex->at(i);if(e.startUs>l.timeTicks||!intersects(prepared_->bounds[i],rect))continue;const StrokeEvent* rendered=&e;if(isAuthoredCubic(e)){if(!prepared_->cubics||!prepared_->cubics->renderEvents.contains(i))return error(ErrorCode::CorruptData,"cubic.preparation-missing");rendered=&prepared_->cubics->renderEvents.at(i);}if(clipsToRoi(e)){
  const auto& b=prepared_->bounds[i];
  const auto x0=std::max(rect.x,b.x),y0=std::max(rect.y,b.y);
  const auto x1=std::min(rect.x+std::int64_t(rect.width),b.x+std::int64_t(b.width));
  const auto y1=std::min(rect.y+std::int64_t(rect.height),b.y+std::int64_t(b.height));
  Rect roi{x0,y0,std::uint64_t(x1-x0),std::uint64_t(y1-y0)};
  auto c=strokeCoverage(*rendered,roi,l.timeTicks,stop);if(!c)return c.error();
  TileData patch;patch.width=std::uint32_t(roi.width);patch.height=std::uint32_t(roi.height);
  patch.format=t.format;patch.profileId=t.profileId;
  const auto pixelBytes=t.format.channelCount()*(t.format.depth==Depth::U16?2u:1u);
  patch.rowStride=roi.width*pixelBytes;patch.bytes.resize(std::size_t(patch.rowStride)*patch.height);
  const auto offset=std::uint64_t(y0-rect.y)*t.rowStride+std::uint64_t(x0-rect.x)*pixelBytes;
  for(unsigned y=0;y<patch.height;++y)std::copy_n(t.bytes.data()+offset+y*t.rowStride,patch.rowStride,patch.bytes.data()+y*patch.rowStride);
  blendCoverage(patch,e,c.value());
  for(unsigned y=0;y<patch.height;++y)std::copy_n(patch.bytes.data()+y*patch.rowStride,patch.rowStride,t.bytes.data()+offset+y*t.rowStride);
  stats_.coveragePixels+=c.value().size();
 }else{
  auto c=strokeCoverage(*rendered,rect,l.timeTicks,stop);if(!c)return c.error();
  stats_.coveragePixels+=c.value().size();blendCoverage(t,e,c.value());
 }
 stats_.rasterizedEvents++;}
 return t;
}
}
