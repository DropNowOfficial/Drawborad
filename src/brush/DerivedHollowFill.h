#pragma once
#include "BucketFill.h"
#include "core/BucketEvent.h"
#include "core/LineartRaster.h"
#include "view/LayerTree.h"
namespace drawborad {
// Validates compatible white-cover semantics, NOT who created the overlay.
// Reconstructs a temporary sampling reference from immutable original + accepted
// white runs; old color events never enter this reference. No persisted tags.
inline Result<Command> derivedHollowFillCommand(const DocumentSnapshot& s,
 NodeId source,NodeId overlay,NodeId colorLayer,Id event,std::uint32_t x,std::uint32_t y,
 ColorValue color,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height,n=w*h;
 if(!w||!h||w>8192||h>8192||n>20000000)return error(ErrorCode::BudgetExceeded,"derived-fill.canvas-limit");
 // Conservative incremental raster/vector storage estimate, excluding source,
 // temporary Document validation/history and subsequent display compositing.
 const auto scratch=10*n+2*FillBudget{}.maxRuns*sizeof(FillRun)+4*FillBudget{}.maxRuns*sizeof(InputSample);
 if(scratch>256ull*1024*1024)return error(ErrorCode::BudgetExceeded,"derived-fill.scratch-limit");
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 if(s.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::UnsupportedFeature,"derived-fill.rgb8");
 const auto& paper=s.spec.background.paperColor;
 if(s.spec.background.transparent||paper.alpha!=1||paper.channels[0]!=1||paper.channels[1]!=1||paper.channels[2]!=1)return error(ErrorCode::UnsupportedFeature,"derived-fill.white-paper");
 const auto& root=s.nodes->at(Document::rootId());
 if(root.locked||!root.visible||root.opacity!=1||root.blend!=BlendMode::Normal||!root.maskId.empty()||root.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"derived-fill.root");
 if(source.empty()||overlay.empty()||colorLayer.empty()||event.empty()||source==overlay||colorLayer==source||colorLayer==overlay||!s.nodes->contains(source)||!s.nodes->contains(overlay))return error(ErrorCode::InvalidInput,"derived-fill.ids");
 const bool existing=s.nodes->contains(colorLayer);
 if(root.children.size()!=(existing?4u:3u)||s.nodes->size()!=root.children.size()+1||root.children[1]!=source||root.children[2]!=overlay||(existing&&root.children[3]!=colorLayer)||root.children[0]==colorLayer)return error(ErrorCode::UnsupportedFeature,"derived-fill.stack");
 const auto neutral=[&](const LayerNode& l){return l.kind==NodeKind::Layer&&l.parent==Document::rootId()&&l.objectId.empty()&&l.children.empty()&&l.content.empty()&&l.sourceResource.empty()&&l.sourceKind==SourceKind::Reconstructed&&l.maskId.empty()&&l.transform==LayerTransform{}&&l.opacity==1&&l.blend==BlendMode::Normal&&l.visible&&!l.locked;};
 if(!neutral(s.nodes->at(root.children[0]))||!neutral(s.nodes->at(overlay))||(existing&&!neutral(s.nodes->at(colorLayer))))return error(ErrorCode::UnsupportedFeature,"derived-fill.layer-properties");
 const auto& ink=s.nodes->at(source);
 if(!ink.locked||!ink.visible||ink.opacity!=1||!ink.maskId.empty()||ink.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"derived-fill.source");
 auto raw=lineartRasterPixels(s,source);if(!raw)return raw.error();
 const StrokeEvent* cover=nullptr;
 for(const auto& e:*s.eventIndex){
  if(e.id==event)return error(ErrorCode::InvalidInput,"derived-fill.event-id");
  if(e.layerId==overlay){if(cover)return error(ErrorCode::UnsupportedFeature,"derived-fill.one-overlay-event");cover=&e;}
  else if(!existing||e.layerId!=colorLayer)return error(ErrorCode::UnsupportedFeature,"derived-fill.other-paint");
 }
 if(!cover)return error(ErrorCode::InvalidInput,"derived-fill.missing-overlay");
 auto coverValid=validateBucketEvent(*cover);if(!coverValid)return coverValid.error();
 if(cover->color.activeChannels!=3||cover->color.profileId!=s.spec.colorSpec.profileId||cover->color.alpha!=1||cover->color.channels[0]!=1||cover->color.channels[1]!=1||cover->color.channels[2]!=1)return error(ErrorCode::UnsupportedFeature,"derived-fill.overlay-not-white");
 // Scratch: reference4N + clearedN + bucket RGBA4N + visitedN, with bounded runs.
 TileData reference;reference.width=std::uint32_t(w);reference.height=std::uint32_t(h);reference.rowStride=w*4;reference.format=s.spec.colorSpec.format;reference.profileId=s.spec.colorSpec.profileId;reference.bytes.resize(std::size_t(n)*4);
 for(std::size_t at=0;at<reference.bytes.size();at+=65536){if(stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");std::copy_n(raw.value().begin()+at,std::min<std::size_t>(65536,reference.bytes.size()-at),reference.bytes.begin()+at);}
 for(std::size_t i=0;i<n;++i){if(i%16384==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");auto p=raw.value().subspan(i*4,4);if(p[3]&&(p[0]!=p[1]||p[1]!=p[2]))return error(ErrorCode::UnsupportedFeature,"derived-fill.grayscale");}
 std::vector<std::uint8_t> cleared(std::size_t(n),0);
 const auto walk=[&](const StrokeEvent& e,bool whiten)->Result<void>{
  auto checked=validateBucketEvent(e);if(!checked)return checked.error();
  for(std::size_t i=0;i<e.geometry.size();i+=2){
   if(stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");
   const auto& a=e.geometry[i];const auto& b=e.geometry[i+1];
   if(a.x<0||a.y<0||b.x>double(w)||a.y>=double(h))return error(ErrorCode::InvalidInput,"derived-fill.run-bounds");
   const auto row=std::uint32_t(a.y);for(auto col=std::uint32_t(a.x);col<std::uint32_t(b.x);++col){
    const auto pos=std::size_t(row)*w+col;
    if(whiten){
     if(col==0||row==0||col+1==w||row+1==h)return error(ErrorCode::InvalidInput,"derived-fill.cover-touches-ink-edge");
     for(unsigned yy=row-1;yy<=row+1;++yy)for(unsigned xx=col-1;xx<=col+1;++xx){const auto at=(std::size_t(yy)*w+xx)*4;if(!raw.value()[at+3]||raw.value()[at]==255)return error(ErrorCode::InvalidInput,"derived-fill.cover-touches-ink-edge");}
     cleared[pos]=1;std::fill_n(reference.bytes.begin()+pos*4,4,255);
    }else if(!cleared[pos])return error(ErrorCode::UnsupportedFeature,"derived-fill.color-outside-cover");
   }
  }return {};
 };
 auto covered=walk(*cover,true);if(!covered)return covered.error();
 for(const auto& e:*s.eventIndex)if(e.layerId==colorLayer){auto checked=walk(e,false);if(!checked)return checked.error();}
 if(x>=w||y>=h||!cleared[std::size_t(y)*w+x])return error(ErrorCode::InvalidInput,"derived-fill.seed-outside-cover");
 Command out;
 if(!existing){LayerNode l;l.id=colorLayer;l.parent=Document::rootId();l.name="Hollow interior colors";l.bounds={0,0,w,h};out.operations.emplace_back(AddNode{l});}
 auto candidate=Document::fromSnapshot(s);if(!candidate)return candidate.error();
 if(!existing){auto added=candidate.value().apply(out,s.revision);if(!added)return added.error();}
 auto fill=bucketFillCommand(candidate.value().snapshot(),colorLayer,event,reference,x,y,0,color,stop);if(!fill)return fill.error();
 for(auto& op:fill.value().operations)if(auto* paint=std::get_if<Paint>(&op)){paint->event.parentRevision=s.revision;auto safe=walk(paint->event,false);if(!safe)return safe.error();out.operations.emplace_back(std::move(op));}
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"derived-fill.allocation");}
}
