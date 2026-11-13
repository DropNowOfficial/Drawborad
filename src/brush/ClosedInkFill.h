#include "core/FillProvenance.h"
#pragma once
#include "MixedLineartStack.h"
#include "ClipPolygonSelection.h"
#include <array>
namespace drawborad {
namespace closed_ink_detail {
// Half-open even/odd crossings. Samples exactly on an edge count as inside;
// the simple polygon validator rejects ambiguous/retraced topology first.
inline bool inside(std::span<const DocumentPoint> polygon,double x,double y) {
 bool value=false;
 for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
  const auto a=polygon[j],b=polygon[i];
  if(clip_polygon_detail::onSegment(a,b,{x,y}))return true;
  if((a.y>y)!=(b.y>y)){
   const auto crossing=static_cast<long double>(a.x)+(static_cast<long double>(y)-a.y)*(static_cast<long double>(b.x)-a.x)/(static_cast<long double>(b.y)-a.y);
   if(static_cast<long double>(x)<crossing)value=!value;
  }
 }
 return value;
}
}
// A new explicit whole-closed-path ink fill, NOT generic flood-fill repair.
// 2x2 geometric coverage is quantized, not identical to the stroke AA filter.
inline Result<Command> closedInkFillCommand(const DocumentSnapshot& full,NodeId selected,
 std::array<Id,4> ids,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
 auto projected=projectControlledLineart(full,stop);if(!projected)return projected.error();
 const auto& view=projected.value();const auto& controlled=view.controlled;
 const auto& children=controlled.nodes->at(Document::rootId()).children;
 if(children.size()!=2)return error(ErrorCode::UnsupportedFeature,"closed-ink.no-derived-regions");
 if(std::find(view.finishing.begin(),view.finishing.end(),selected)==view.finishing.end()||!nativeLayerPaintable(full,selected))return error(ErrorCode::InvalidInput,"closed-ink.visible-editable-finishing-required");
 const StrokeEvent* outline=nullptr;
 std::uint64_t oldPoints=0;
 std::set<Id> used;
 for(const auto& [id,n]:*full.nodes){(void)n;used.insert(id);}
 for(const auto& e:*full.eventIndex){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
  oldPoints+=e.geometry.size();used.insert(e.id);
  if(e.layerId==selected){if(outline)return error(ErrorCode::UnsupportedFeature,"closed-ink.single-stroke-required");outline=&e;}
 }
 for(const auto id:ids)if(id.empty()||!used.insert(id).second)return error(ErrorCode::InvalidInput,"closed-ink.event-ids");
 if(!outline||outline->engineVersion!=19||outline->toolId!="smooth-ink-aa"||outline->color.alpha!=1||outline->style.opacity!=1||outline->geometry.size()<4||outline->geometry.size()>4096)return error(ErrorCode::UnsupportedFeature,"closed-ink.opaque-smooth-outline-required");
 const auto& g=outline->geometry;
 if(g.front().x!=g.back().x||g.front().y!=g.back().y)return error(ErrorCode::InvalidInput,"closed-ink.explicit-closure-required");
 // Collapse only adjacent duplicate positions in a temporary geometry view.
 // Raw samples (including release duplicates) are preserved and all validated.
 std::vector<DocumentPoint> polygon;polygon.reserve(std::min<std::size_t>(g.size(),257));
 for(const auto& point:g){
  if(!std::isfinite(point.pressure)||point.pressure<=0||outline->style.width*point.pressure<1)return error(ErrorCode::UnsupportedFeature,"closed-ink.effective-width-at-least-one");
  if(polygon.empty()||polygon.back().x!=point.x||polygon.back().y!=point.y){
   if(polygon.size()>=257)return error(ErrorCode::BudgetExceeded,"closed-ink.vertex-budget");
   polygon.push_back({point.x,point.y});
  }
 }
 if(polygon.size()<4||polygon.front().x!=polygon.back().x||polygon.front().y!=polygon.back().y)return error(ErrorCode::InvalidInput,"closed-ink.explicit-closure-required");
 polygon.pop_back();
 const auto w=full.spec.pixelSize.width,h=full.spec.pixelSize.height;
 auto shape=validateClipPolygon(polygon,std::uint32_t(w),std::uint32_t(h),stop);if(!shape)return shape.error();
 for(std::size_t i=0;i<polygon.size();++i)for(std::size_t j=i+1;j<polygon.size();++j)
  if(polygon[i].x==polygon[j].x&&polygon[i].y==polygon[j].y)return error(ErrorCode::InvalidInput,"closed-ink.duplicate-vertex");
 const auto source=children[1],target=children[0];
 auto raw=lineartRasterPixels(controlled,source);if(!raw)return raw.error();
 // White-only is deliberately global: no inferred internal partitions, gaps,
 // erased regions or separate original-ink islands can be silently crossed.
 for(std::size_t i=0;i<raw.value().size();i+=4){
  if(i%65536==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
  if(raw.value()[i+3]&&(raw.value()[i]!=255||raw.value()[i+1]!=255||raw.value()[i+2]!=255))return error(ErrorCode::UnsupportedFeature,"closed-ink.original-must-be-white");
 }
 double minx=double(w),miny=double(h),maxx=0,maxy=0;
 for(auto p:polygon){minx=std::min(minx,p.x);miny=std::min(miny,p.y);maxx=std::max(maxx,p.x);maxy=std::max(maxy,p.y);}
 const auto x0=std::uint32_t(std::floor(minx)),y0=std::uint32_t(std::floor(miny));
 const auto x1=std::uint32_t(std::ceil(maxx)),y1=std::uint32_t(std::ceil(maxy));
 const std::uint64_t rw=x1-x0,rh=y1-y0,n=rw*rh;
 if(!n||n>128ull*1024*1024/(4*polygon.size()))return error(ErrorCode::BudgetExceeded,"closed-ink.work-budget");
 std::vector<std::uint8_t> coverage(std::size_t(n),0);
 std::uint64_t pixels=0;
 for(std::uint32_t y=y0;y<y1;++y){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
  for(std::uint32_t x=x0;x<x1;++x){
   unsigned count=0;for(double dy:{.25,.75})for(double dx:{.25,.75})count+=closed_ink_detail::inside(polygon,x+dx,y+dy);
   coverage[std::size_t(y-y0)*rw+x-x0]=std::uint8_t(count);pixels+=count!=0;
  }
 }
 if(!pixels)return error(ErrorCode::InvalidInput,"closed-ink.empty-coverage");
 // Never repaint an existing lower-layer pixel, including translucent paint.
 for(const auto& e:*controlled.eventIndex){
  if(e.layerId!=target)return error(ErrorCode::UnsupportedFeature,"closed-ink.target-events");
  for(std::size_t i=0;i<e.geometry.size();i+=2){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
   const auto y=std::uint32_t(e.geometry[i].y);if(y<y0||y>=y1)continue;
   const auto begin=std::max(x0,std::uint32_t(e.geometry[i].x)),end=std::min(x1,std::uint32_t(e.geometry[i+1].x));
   for(auto x=begin;x<end;++x)if(coverage[std::size_t(y-y0)*rw+x-x0])return error(ErrorCode::UnsupportedFeature,"closed-ink.existing-fill-overlap");
  }
 }
 Command command;std::uint64_t addedPoints=0;
 for(unsigned level=1;level<=4;++level){
  StrokeEvent e;e.id=ids[level-1];e.layerId=target;e.objectId=controlled.nodes->at(target).objectId;e.engineVersion=8;e.toolId="bucket";e.parentRevision=controlled.revision;e.color=outline->color;e.color.alpha=double(level)/4;
  for(std::uint32_t y=y0;y<y1;++y){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"closed-ink.cancelled");
   for(std::uint32_t x=x0;x<x1;){
    if(coverage[std::size_t(y-y0)*rw+x-x0]!=level){++x;continue;}
    const auto begin=x;while(x<x1&&coverage[std::size_t(y-y0)*rw+x-x0]==level)++x;
    if(e.geometry.size()/2>=FillBudget{}.maxRuns||oldPoints+addedPoints+2>1000000)return error(ErrorCode::BudgetExceeded,"closed-ink.geometry-budget");
    e.geometry.push_back({double(begin),double(y),1,{},{},0,0,0});e.geometry.push_back({double(x),double(y),1,{},{},0,0,0});addedPoints+=2;
   }
  }
  if(!e.geometry.empty())command.operations.emplace_back(Paint{std::move(e),{x0,y0,rw,rh}});
 }
 if(full.eventIndex->size()+command.operations.size()>8192)return error(ErrorCode::BudgetExceeded,"closed-ink.event-budget");
 auto restored=restoreControlledCommand(full,view,std::move(command),stop);if(!restored)return restored.error();
 FillOperation record;record.target=target;record.source=source;record.boundaryLayer=selected;record.boundaryEvent=outline->id;record.boundaryDigest=fill_provenance::eventDigest(*outline);record.sourceResource=full.nodes->at(source).sourceResource;record.sourceDigest=sha256(*full.resources->at(record.sourceResource).bytes);record.contextDigest=fill_provenance::contextDigest(full,record);
 for(const auto&op:restored.value().operations){const auto&p=std::get<Paint>(op);record.members.emplace_back(p.event.id,fill_provenance::eventDigest(p.event));}if(record.members.empty())return error(ErrorCode::InvalidInput,"fill-source.empty");record.id=record.members.front().first;restored.value().operations.emplace_back(RecordClosedInkFill{std::move(record)});return restored;

}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"closed-ink.allocation");}
}
