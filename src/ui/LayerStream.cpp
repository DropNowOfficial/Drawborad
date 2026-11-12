#include "LayerStream.h"
#include "view/LayerTree.h"
#include <algorithm>
#include <cmath>
namespace drawborad {
Result<NativeLayerStream> NativeLayerStream::create(const DocumentSnapshot& s,EventLimit limit,MergedBudget budget){
 if(limit.revision!=s.revision)return error(ErrorCode::StaleRevision,"layer-stream.revision");
 try{
  // Hidden layers also cost rendering work in an editable layer archive.
  if(!s.nodes)return error(ErrorCode::CorruptData,"document.structure");
  if(s.nodes->size()>4096)return error(ErrorCode::BudgetExceeded,"merged.structure-limit");
  auto visibleNodes=std::make_shared<NodeMap>(*s.nodes);for(auto& [id,n]:*visibleNodes){(void)id;n.visible=true;}
  auto all=s;all.nodes=visibleNodes;auto p=planMergedDocument(all,budget);if(!p)return p.error();
  NativeLayerStream stream;stream.snapshot_=s;stream.limit_=limit;
  auto views=croppedRaster::PreparedViews::prepare(s);if(!views)return views.error();stream.croppedViews_=std::move(views.value());
  stream.order_=layerOrder(s);std::reverse(stream.order_.begin(),stream.order_.end());
  if(!s.spec.background.transparent)stream.order_.push_back(NodeId{}); // Bottom paper, not a document node.
  auto prepared=stream.renderer_.prepare(s);if(!prepared)return prepared.error();
  return stream;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"layer-stream.allocation");}
}
Result<ArchiveLayer> NativeLayerStream::read(std::size_t index,std::stop_token stop){
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"layer-stream.cancelled");
 if(index>=order_.size())return error(ErrorCode::InvalidInput,"layer-stream.index");
 try{
  const auto& s=snapshot_;const auto size=s.spec.pixelSize;const auto id=order_[index];
  ArchiveLayer out;auto& image=out.pixels;
  if(!id.empty() && s.nodes->at(id).stage==croppedRaster::Stage) {
   const auto& node=s.nodes->at(id);const auto* crop=croppedViews_.find(s,id);
   if(!crop)return error(ErrorCode::CorruptData,"crop.prepared-view");
   out.name=node.name;out.visible=layerVisible(s,id);out.opacity=node.opacity;out.blend=node.blend;
   out.x=crop->bounds.x;out.y=crop->bounds.y;
   image.width=std::uint32_t(crop->bounds.width);image.height=std::uint32_t(crop->bounds.height);image.rowStride=crop->bounds.width*4;
   image.format=s.spec.colorSpec.format;image.profileId=s.spec.colorSpec.profileId;image.engineVersion=2;
   image.bytes.assign(crop->rgba.begin(),crop->rgba.end());
   auto masked=nativeMaskedTile(s,id,std::move(image),out.x,out.y,&croppedViews_);if(!masked)return masked.error();
   out.pixels=std::move(masked.value());return out;
  }
  image.width=std::uint32_t(size.width);image.height=std::uint32_t(size.height);image.rowStride=size.width*4;
  image.format=s.spec.colorSpec.format;image.profileId=s.spec.colorSpec.profileId;image.engineVersion=2;
  image.bytes.resize(std::size_t(image.rowStride*size.height));
  if(id.empty()){
   out.name="Paper";
   for(std::size_t i=0;i<image.bytes.size();i+=4){for(unsigned c=0;c<3;c++)image.bytes[i+c]=std::uint8_t(std::lround(s.spec.background.paperColor.channels[c]*255));image.bytes[i+3]=255;}
   return out;
  }
  const auto& node=s.nodes->at(id);out.name=node.name;out.visible=layerVisible(s,id);out.opacity=node.opacity;out.blend=node.blend;
  const bool cropped=node.stage==croppedRaster::Stage;
  const bool raster=node.stage==LineartStage || cropped;croppedRaster::View crop;
  std::span<const std::uint8_t> raw;
  if(cropped){const auto* v=croppedViews_.find(s,id);if(!v)return error(ErrorCode::CorruptData,"crop.prepared-view");crop=*v;}
  else if(raster){auto source=lineartRasterPixels(s,id);if(!source)return source.error();raw=source.value();}
  for(std::uint64_t y=0;y<size.height;y+=TileSide)for(std::uint64_t x=0;x<size.width;x+=TileSide){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"layer-stream.cancelled");
   Rect rect{std::int64_t(x),std::int64_t(y),std::min<std::uint64_t>(TileSide,size.width-x),std::min<std::uint64_t>(TileSide,size.height-y)};
   Result<TileData> tile=error(ErrorCode::UnsupportedFeature,"layer-stream.tile");
   if(raster){
    TileData t;t.width=std::uint32_t(rect.width);t.height=std::uint32_t(rect.height);t.rowStride=rect.width*4;t.format=image.format;t.profileId=image.profileId;
    t.bytes.resize(std::size_t(t.rowStride*t.height));
    if(cropped){auto copied=croppedRaster::copyRegion(crop,rect,t.bytes);if(!copied)return copied.error();}
    else for(std::uint32_t row=0;row<t.height;row++)std::copy_n(raw.begin()+std::ptrdiff_t(((y+row)*size.width+x)*4),std::size_t(t.rowStride),t.bytes.begin()+std::ptrdiff_t(row*t.rowStride));
    tile=std::move(t);
   }else tile=renderer_.renderRegion(s,id,rect,limit_,stop);
   if(!tile)return tile.error();
   auto masked=nativeMaskedTile(s,id,std::move(tile.value()),rect.x,rect.y,&croppedViews_);if(!masked)return masked.error();
   const auto& part=masked.value();for(std::uint32_t row=0;row<part.height;row++)
    std::copy_n(part.bytes.begin()+std::ptrdiff_t(row*part.rowStride),std::size_t(part.rowStride),image.bytes.begin()+std::ptrdiff_t((y+row)*image.rowStride+x*4));
  }
  return out;
 }catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"layer-stream.allocation");}
}
}
