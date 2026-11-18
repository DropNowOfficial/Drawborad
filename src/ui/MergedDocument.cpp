#include "core/ArtistOptIn.h"
#include "core/PersistentRasterPaint.h"
#include "MergedDocument.h"
#include "render/DryInkCoverage.h"
#include "render/PixelBlend.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
namespace drawborad {
namespace {
std::int64_t tileAt(std::int64_t x){return x>=0?x/TileSide:-((-x+TileSide-1)/TileSide);}
bool addWithin(std::uint64_t& value,std::uint64_t increment,std::uint64_t cap){
 if(value>cap || increment>cap-value)return false;
 value+=increment;return true;
}
bool clipsStrokeToRoi(const StrokeEvent& e){
 return (e.engineVersion==2 && (e.toolId=="marker" || e.toolId=="highlight")) ||
        e.engineVersion==10 || e.engineVersion==12 || e.engineVersion==19 ||
        e.engineVersion==21 || e.engineVersion==22 || hasLinearOpticalBlend(e);
}
Result<MergedPlan> plan(const DocumentSnapshot& s,MergedBudget budget){
 if(!s.nodes || !s.eventIndex || !s.resources)return error(ErrorCode::CorruptData,"document.structure");
 if(s.nodes->size()>4096 || s.eventIndex->size()>65536)return error(ErrorCode::BudgetExceeded,"merged.structure-limit");
 std::uint64_t points=0;
 for(const auto& e:*s.eventIndex)if(!addWithin(points,e.geometry.size(),budget.maxGeometryPoints))return error(ErrorCode::BudgetExceeded,"merged.geometry-limit");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 const auto size=s.spec.pixelSize;
 if(s.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::UnsupportedFeature,"merged.rgb8-required");
 auto pixels=checkedMultiply(size.width,size.height);if(!pixels)return pixels.error();
 if(size.width>budget.maxAxis||size.height>budget.maxAxis||pixels.value()>budget.maxPixels)return error(ErrorCode::BudgetExceeded,"merged.canvas-limit");
 auto bytes=checkedMultiply(pixels.value(),4);if(!bytes)return bytes.error();
 if(size.width>UINT32_MAX || size.height>UINT32_MAX || bytes.value()>std::numeric_limits<std::size_t>::max())return error(ErrorCode::BudgetExceeded,"merged.layout");
 // nativeCompositingSupported scans every binary ancestor mask in full.
 // It runs once per node here, then again inside nativeMaskedTile for each
 // rendered layer tile. Account for these scans BEFORE doing any all_of.
 std::uint64_t work=0;
 const auto tileCount=checkedMultiply((size.width+TileSide-1)/TileSide,(size.height+TileSide-1)/TileSide);
 if(!tileCount)return tileCount.error();
 // Snapshot validation, planner preparation and render preparation each scan once.
 // Account four scans conservatively; no per-tile hashing with prepared views.
 for(const auto& [id,node]:*s.nodes)if(croppedRaster::isStage(node.stage)){
  (void)id;const auto& raw=s.resources->at(node.sourceResource);std::uint64_t scanned=raw.bytes->size();
  for(const auto dep:raw.dependencies)if(!addWithin(scanned,s.resources->at(dep).bytes->size(),budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.crop-validation-work");
  const auto repeats=checkedMultiply(scanned,4);
  if(!repeats||!addWithin(work,repeats.value(),budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.crop-validation-work");
 }
 for(const auto& [id,node]:*s.nodes){
  std::uint64_t scans=0;
  for(auto ancestor=id;!ancestor.empty();ancestor=s.nodes->at(ancestor).parent){
   const auto& current=s.nodes->at(ancestor);
   if(current.kind!=NodeKind::Layer && !current.maskId.empty()){
    const auto bytesToScan=s.resources->at(current.maskId).bytes->size();
    if(!addWithin(scans,bytesToScan,budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.mask-scan-limit");
   }
  }
  if(!addWithin(work,scans,budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.mask-scan-limit");
  if(node.kind==NodeKind::Layer && layerVisible(s,id)){
   auto repeats=checkedMultiply(scans,tileCount.value());
   if(!repeats || !addWithin(work,repeats.value(),budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.mask-scan-limit");
  }
 }
 auto history=persistentRaster::Prepared::prepare(s);if(!history)return history.error();
 auto prepared=croppedRaster::PreparedViews::prepare(s);if(!prepared)return prepared.error();
 for(const auto& [id,node]:*s.nodes)
  if(node.transform!=LayerTransform{} || !nativeCompositingSupported(s,id,&prepared.value()))return error(ErrorCode::UnsupportedFeature,"merged.native-compositing-required");
 MergedPlan out;out.mergedBytes=bytes.value();
 if(!addWithin(work,history.value().work(),budget.maxRasterWork)||!addWithin(out.indexEntries,history.value().indexEntries(),budget.maxIndexEntries))return error(ErrorCode::BudgetExceeded,"paint-history.merged-budget");
 // One RGBA tile + uint16 coverage; two extra RGBA tiles of conservative slack.
 const auto tilePixels=std::min<std::uint64_t>(TileSide,size.width)*std::min<std::uint64_t>(TileSide,size.height);
 const bool hasDryInk=std::any_of(s.eventIndex->begin(),s.eventIndex->end(),[](const auto& e){return e.engineVersion==12;});
 out.tileScratchBytes=tilePixels*(14+(hasDryInk?DryInkNearestBytesPerPixel:0));
 struct LayerBbox { std::int64_t x0,y0,x1,y1; bool valid=false; };
 std::map<NodeId,LayerBbox> artistLayerBounds;
 for(const auto& [id,node]:*s.nodes)if(node.kind==NodeKind::Layer && layerVisible(s,id)){
  const bool raster=node.stage==LineartStage || croppedRaster::isStage(node.stage) || node.stage==persistentRaster::Stage;
  if(!raster && isArtistStage(node.stage)){
   artistLayerBounds.emplace(id,LayerBbox{std::int64_t(size.width),std::int64_t(size.height),0,0,false});
   continue;
  }
  if(!addWithin(work,pixels.value(),budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.composite-work-limit");
 }
 for(const auto& e:*s.eventIndex){
  if(s.nodes->contains(e.layerId) && (s.nodes->at(e.layerId).stage==LineartStage || croppedRaster::isStage(s.nodes->at(e.layerId).stage)||s.nodes->at(e.layerId).stage==persistentRaster::Stage))return error(ErrorCode::UnsupportedFeature,"lineart.paint-events");
  auto b=strokeBounds(e);if(!b)return b.error();
  const auto& r=b.value();
  const auto nx=std::uint64_t(tileAt(r.x+std::int64_t(r.width)-1)-tileAt(r.x)+1);
  const auto ny=std::uint64_t(tileAt(r.y+std::int64_t(r.height)-1)-tileAt(r.y)+1);
  auto count=checkedMultiply(nx,ny);if(!count)return count.error();
  if(count.value()>4096 || !addWithin(out.indexEntries,count.value(),budget.maxIndexEntries))return error(ErrorCode::BudgetExceeded,"merged.index-limit");
  // Clipped engines partition their true canvas-clamped ROI across intersected tiles;
  // legacy unclipped engines retain full-tile coverage accounting.
  const auto cx0=std::max<std::int64_t>(0,r.x);
  const auto cy0=std::max<std::int64_t>(0,r.y);
  const auto cx1=std::min<std::int64_t>(size.width,r.x+std::int64_t(r.width));
  const auto cy1=std::min<std::int64_t>(size.height,r.y+std::int64_t(r.height));
  const auto clippedWidth=std::max<std::int64_t>(0,cx1-cx0);
  const auto clippedHeight=std::max<std::int64_t>(0,cy1-cy0);
  if(clippedWidth>0 && clippedHeight>0){
   if(auto bit=artistLayerBounds.find(e.layerId);bit!=artistLayerBounds.end()){
    bit->second.x0=std::min(bit->second.x0,cx0);bit->second.y0=std::min(bit->second.y0,cy0);
    bit->second.x1=std::max(bit->second.x1,cx1);bit->second.y1=std::max(bit->second.y1,cy1);
    bit->second.valid=true;
   }
  }
  const auto roiPixels=checkedMultiply(std::uint64_t(clippedWidth),std::uint64_t(clippedHeight));
  if(!roiPixels)return roiPixels.error();
  auto coverage=clipsStrokeToRoi(e)?roiPixels:checkedMultiply(count.value(),tilePixels);
  if(!coverage || !addWithin(out.coveragePixels,coverage.value(),budget.maxCoveragePixels))return error(ErrorCode::BudgetExceeded,"merged.coverage-limit");
  // Account per-tile geometry traversal plus partitioned canvas ROI / footprint work
  // (eliminating the eventWork * count quadratic blow-up and full-tile dry-ink inflation).
  auto scanWork=checkedMultiply(std::uint64_t(e.geometry.size()),count.value());
  if(!scanWork || !addWithin(work,scanWork.value(),budget.maxRasterWork))return error(ErrorCode::BudgetExceeded,"merged.raster-work-limit");
  long double pixelWork=0;
  if(e.engineVersion==8){
   pixelWork=static_cast<long double>(roiPixels.value());
  }else if(e.engineVersion==10 || e.engineVersion==12 || e.engineVersion==19 || e.engineVersion==21){
   const auto side=std::min<std::uint64_t>(TileSide,std::uint64_t(std::ceil(e.style.width))+4);
   pixelWork=static_cast<long double>(side)*side;
   for(std::size_t i=1;i<e.geometry.size();++i){
    const auto& a=e.geometry[i-1];const auto& b0=e.geometry[i];
    const auto segLen=std::hypot(static_cast<long double>(b0.x)-a.x,static_cast<long double>(b0.y)-a.y);
    pixelWork+=(segLen+static_cast<long double>(side))*side;
   }
   if(e.engineVersion==12)pixelWork+=static_cast<long double>(roiPixels.value())*6.L;
  }else{
   const auto side=std::min<std::uint64_t>(TileSide,std::uint64_t(std::ceil(e.style.width))+4);
   const auto spacing=std::max(64.,std::floor(std::round(e.style.width*128)/2));
   long double dabs=1;
   for(std::size_t i=1;i<e.geometry.size();++i){
    const auto& a=e.geometry[i-1];const auto& b0=e.geometry[i];
    dabs+=std::max(1.L,std::ceil((std::abs(b0.x-a.x)+std::abs(b0.y-a.y))*256.L/spacing)+2);
   }
   pixelWork=dabs*side*side;
  }
  if(pixelWork>static_cast<long double>(budget.maxRasterWork) ||
     !addWithin(work,std::uint64_t(pixelWork),budget.maxRasterWork))
   return error(ErrorCode::BudgetExceeded,"merged.raster-work-limit");
 }
 for(const auto& [lid,box]:artistLayerBounds){
  (void)lid;if(!box.valid)continue;
  const auto activeArea=checkedMultiply(std::uint64_t(box.x1-box.x0),std::uint64_t(box.y1-box.y0));
  if(!activeArea || !addWithin(work,activeArea.value(),budget.maxRasterWork))
   return error(ErrorCode::BudgetExceeded,"merged.composite-work-limit");
 }
 out.estimatedBytes=out.mergedBytes;
 if(!addWithin(out.estimatedBytes,history.value().estimatedBytes(),budget.maxBytes))return error(ErrorCode::BudgetExceeded,"paint-history.merged-memory");
 // Index map nodes/vectors, candidate copies, bounds and node traversal allowance.
 const auto cropCount=std::count_if(s.nodes->begin(),s.nodes->end(),[](const auto& item){return croppedRaster::isStage(item.second.stage);});
 // Only cropped layers allocate entries in PreparedViews; legacy nodes retain
 // their original 128-byte plan estimate. Do not charge every old layer for ROI.
 const auto indexBytes=out.indexEntries*256+s.eventIndex->size()*128+s.nodes->size()*128+std::uint64_t(cropCount)*256+65536;
 if(!addWithin(out.estimatedBytes,out.tileScratchBytes,budget.maxBytes) || !addWithin(out.estimatedBytes,indexBytes,budget.maxBytes))return error(ErrorCode::BudgetExceeded,"merged.memory-limit");
 return out;
}
}
Result<MergedPlan> planMergedDocument(const DocumentSnapshot& s,MergedBudget budget){
 try{return plan(s,budget);}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"merged.allocation");}
}
Result<MergedDocument> renderMergedDocument(const DocumentSnapshot& s,EventLimit limit,MergedBudget budget,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"merged.cancelled");
 if(limit.revision!=s.revision)return error(ErrorCode::StaleRevision,"merged.revision");
 auto p=planMergedDocument(s,budget);if(!p)return p.error();
 try{
  auto history=persistentRaster::Prepared::prepare(s);if(!history)return history.error();
  auto prepared=croppedRaster::PreparedViews::prepare(s);if(!prepared)return prepared.error();
  MergedDocument out;out.plan=p.value();auto& dest=out.merged;
  const auto size=s.spec.pixelSize;dest.width=std::uint32_t(size.width);dest.height=std::uint32_t(size.height);
  dest.rowStride=size.width*4;dest.format=s.spec.colorSpec.format;dest.profileId=s.spec.colorSpec.profileId;dest.engineVersion=2;
  dest.bytes.resize(std::size_t(out.plan.mergedBytes));
  if(!s.spec.background.transparent)for(std::size_t at=0;at<dest.bytes.size();at+=4){
   for(unsigned c=0;c<3;c++)dest.bytes[at+c]=std::uint8_t(std::lround(s.spec.background.paperColor.channels[c]*255));
   dest.bytes[at+3]=255;
  }
  ReferenceRenderer renderer;
  for(auto id:layerOrder(s)){
   if(!layerVisible(s,id))continue;
   const auto& node=s.nodes->at(id);
   const auto opacity=std::uint8_t(std::lround(node.opacity*255));
   const bool linearComposite=isLinearCompositeStage(node.stage);
   const bool artistSparse=isArtistStage(node.stage);
   for(std::uint64_t y=0;y<size.height;y+=TileSide)for(std::uint64_t x=0;x<size.width;x+=TileSide){
    if(stop.stop_requested())return error(ErrorCode::Cancelled,"merged.cancelled");
    Rect rect{std::int64_t(x),std::int64_t(y),std::min<std::uint64_t>(TileSide,size.width-x),std::min<std::uint64_t>(TileSide,size.height-y)};
    const bool cropped=croppedRaster::isStage(node.stage);
    const bool historyLayer=node.stage==persistentRaster::Stage;
    const bool raster=node.stage==LineartStage || cropped || historyLayer;
    if(!raster && artistSparse){
     auto cands=renderer.candidates(s,{id,tileAt(std::int64_t(x)),tileAt(std::int64_t(y)),0},stop);
     if(!cands)return cands.error();
     if(cands.value().empty())continue;
    }
    Result<TileData> tile=error(ErrorCode::UnsupportedFeature,"lineart.tile");
    if(raster){
     std::span<const std::uint8_t> raw;croppedRaster::View crop;
     if(cropped){const auto* v=prepared.value().find(s,id);if(!v)return error(ErrorCode::CorruptData,"crop.prepared-view");crop=*v;}
     else if(!historyLayer) {auto v=lineartRasterPixels(s,id);if(!v)return v.error();raw=v.value();}
     TileData t;t.width=std::uint32_t(rect.width);t.height=std::uint32_t(rect.height);t.rowStride=rect.width*4;
     t.format=s.spec.colorSpec.format;t.profileId=s.spec.colorSpec.profileId;t.bytes.resize(std::size_t(t.rowStride*t.height));
     if(historyLayer){auto copied=history.value().render(s,id,rect,t.bytes,stop);if(!copied)return copied.error();}
     else if(cropped){auto copied=croppedRaster::copyRegion(crop,rect,t.bytes);if(!copied)return copied.error();}
     else for(std::uint32_t row=0;row<t.height;row++)std::copy_n(raw.begin()+std::ptrdiff_t(((y+row)*size.width+x)*4),std::size_t(t.rowStride),t.bytes.begin()+std::ptrdiff_t(row*t.rowStride));
     tile=std::move(t);
    }else tile=renderer.renderRegion(s,id,rect,limit,stop);
    if(!tile)return tile.error();
    auto masked=nativeMaskedTile(s,id,std::move(tile.value()),rect.x,rect.y,&prepared.value());if(!masked)return masked.error();
    const auto& src=masked.value();
    for(std::uint32_t row=0;row<src.height;row++)for(std::uint32_t col=0;col<src.width;col++){
     const auto from=std::size_t(row*src.rowStride+col*4),to=std::size_t((y+row)*dest.rowStride+(x+col)*4);
     compositeRgba8Pixel(dest.bytes.data()+to,src.bytes.data()+from,opacity,node.blend,linearComposite);
    }
   }
  }
  out.statistics=renderer.statistics();return out;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"merged.allocation");}
}
}
