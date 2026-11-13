#pragma once
#include "HollowInkRegion.h"
#include "core/LineartRaster.h"
#include "view/LayerTree.h"
namespace drawborad {
// Explicit white-paper hollowing overlay. The original locked lineart resource
// is never changed. One command creates a separate editable overlay + event.
// This is not transparency erasure or automatic color-source classification.
inline Result<Command> hollowInkOverlayCommand(const DocumentSnapshot& s,NodeId source,
 NodeId overlay,Id event,const FillMask& selection,unsigned radius,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 const auto& paper=s.spec.background.paperColor;
 if(s.spec.background.transparent||paper.alpha!=1||paper.channels[0]!=1||paper.channels[1]!=1||paper.channels[2]!=1)return error(ErrorCode::UnsupportedFeature,"hollow.opaque-white-paper-required");
 if(s.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::UnsupportedFeature,"hollow.rgb8-required");
 if(overlay.empty()||event.empty()||s.nodes->contains(overlay)||!s.nodes->contains(source))return error(ErrorCode::InvalidInput,"hollow.ids");
 const auto& node=s.nodes->at(source);const auto& root=s.nodes->at(Document::rootId());
 if(!root.visible||root.opacity!=1||root.blend!=BlendMode::Normal||!root.maskId.empty()||root.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"hollow.neutral-root-required");
 if(root.children.empty()||root.children.back()!=source||root.locked||!node.locked||!layerVisible(s,source)||node.opacity!=1||!node.maskId.empty()||node.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"hollow.source-must-be-top-locked-lineart");
 auto raw=lineartRasterPixels(s,source);if(!raw)return raw.error();
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;if(w>8192||h>8192||w*h>20000000)return error(ErrorCode::BudgetExceeded,"hollow.canvas-limit");
 std::vector<std::uint8_t> ink(std::size_t(w*h),0);
 for(std::size_t i=0;i<ink.size();i++){
  if((i%16384)==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"hollow.cancelled");
  const auto p=raw.value().subspan(i*4,4);
  if(p[3]&&(p[0]!=p[1]||p[1]!=p[2]))return error(ErrorCode::UnsupportedFeature,"hollow.declared-grayscale-required");
  ink[i]=p[3]&&p[0]<255;
 }
 auto hollow=hollowInkRegion(ink,selection,std::uint32_t(w),std::uint32_t(h),radius,stop);if(!hollow)return hollow.error();
 const auto& clear=hollow.value().toClear;if(clear.runs.empty())return error(ErrorCode::InvalidInput,"hollow.no-safe-interior");
 if(s.eventIndex->size()>=8192)return error(ErrorCode::BudgetExceeded,"hollow.events");
 std::uint64_t points=0,tick=0;for(const auto& e:*s.eventIndex){if(e.id==event)return error(ErrorCode::InvalidInput,"hollow.duplicate-event");points+=e.geometry.size();if(points>1000000||e.endUs==UINT64_MAX)return error(ErrorCode::BudgetExceeded,"hollow.timeline");tick=std::max(tick,e.endUs+1);}
 if(clear.runs.size()*2>1000000-points)return error(ErrorCode::BudgetExceeded,"hollow.geometry");
 LayerNode layer;layer.id=overlay;layer.parent=Document::rootId();layer.name="Selected ink hollow - white paper overlay";layer.bounds={0,0,w,h};
 StrokeEvent paint;paint.id=event;paint.layerId=overlay;paint.parentRevision=s.revision;paint.engineVersion=8;paint.toolId="bucket";paint.startUs=paint.endUs=tick;paint.color.activeChannels=3;paint.color.channels={1,1,1,0};paint.color.alpha=1;paint.color.profileId=s.spec.colorSpec.profileId;
 for(const auto& run:clear.runs){paint.geometry.push_back({double(run.x),double(run.y),1,{},{},tick,0,0});paint.geometry.push_back({double(run.x+run.length),double(run.y),1,{},{},tick,0,0});}
 Command command;command.operations={AddNode{layer},Paint{std::move(paint),{0,0,w,h}}};return command;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"hollow.allocation");}
}
