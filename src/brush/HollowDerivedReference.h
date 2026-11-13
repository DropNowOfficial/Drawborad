#pragma once
#include "BucketFill.h"
#include "core/BucketEvent.h"
#include "core/LineartRaster.h"
#include "view/LayerTree.h"
namespace drawborad {
inline bool hollowNeutralPaintLayer(const LayerNode& l){return l.kind==NodeKind::Layer&&l.parent==Document::rootId()&&l.objectId.empty()&&l.children.empty()&&l.content.empty()&&l.sourceResource.empty()&&l.sourceKind==SourceKind::Reconstructed&&l.maskId.empty()&&l.transform==LayerTransform{}&&l.opacity==1&&l.blend==BlendMode::Normal&&l.visible&&!l.locked;}
inline Result<void> validateOriginalWhitePaint(const StrokeEvent& e,const DocumentSnapshot& s,
 std::span<const std::uint8_t> raw,std::stop_token stop={}){
 auto checked=validateBucketEvent(e);if(!checked)return checked.error();
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(raw.size()!=w*h*4||e.color.activeChannels!=3||e.color.profileId!=s.spec.colorSpec.profileId)return error(ErrorCode::UnsupportedFeature,"derived-fill.lower-properties");
 for(std::size_t i=0;i<e.geometry.size();i+=2){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");
  const auto& a=e.geometry[i];const auto& b=e.geometry[i+1];
  if(a.y>=double(h)||b.x>double(w))return error(ErrorCode::InvalidInput,"derived-fill.lower-bounds");
  for(auto x=std::uint32_t(a.x);x<std::uint32_t(b.x);++x){const auto k=(std::size_t(a.y)*w+x)*4;
   if(raw[k+3]&&(raw[k]!=255||raw[k+1]!=255||raw[k+2]!=255))return error(ErrorCode::UnsupportedFeature,"derived-fill.lower-outside-original-white");
  }
 }
 return {};
}
// Compatibility validation, not provenance certification. Original resources,
// accepted cover events and colors are never rewritten. Only temporary sampling
// pixels are composed; existing colored paint never enters the reference.
struct HollowDerivedReference { TileData reference; std::vector<std::uint8_t> cleared; };
inline Result<HollowDerivedReference> hollowDerivedReference(const DocumentSnapshot& s,
 NodeId source,NodeId overlay,NodeId colorLayer={},std::stop_token stop={}) try {
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
 if(source.empty()||overlay.empty()||source==overlay||(!colorLayer.empty()&&(colorLayer==source||colorLayer==overlay))||!s.nodes->contains(source)||!s.nodes->contains(overlay))return error(ErrorCode::InvalidInput,"derived-fill.ids");
 const bool existing=s.nodes->contains(colorLayer);
 if(root.children.size()!=(existing?4u:3u)||s.nodes->size()!=root.children.size()+1||root.children[1]!=source||root.children[2]!=overlay||(existing&&root.children[3]!=colorLayer)||root.children[0]==colorLayer)return error(ErrorCode::UnsupportedFeature,"derived-fill.stack");
 if(!hollowNeutralPaintLayer(s.nodes->at(root.children[0]))||!hollowNeutralPaintLayer(s.nodes->at(overlay))||(existing&&!hollowNeutralPaintLayer(s.nodes->at(colorLayer))))return error(ErrorCode::UnsupportedFeature,"derived-fill.layer-properties");
 const auto& ink=s.nodes->at(source);
 if(!ink.locked||!ink.visible||ink.opacity!=1||!ink.maskId.empty()||ink.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"derived-fill.source");
 auto raw=lineartRasterPixels(s,source);if(!raw)return raw.error();
 std::vector<const StrokeEvent*> covers;
 for(const auto& e:*s.eventIndex){
   if(e.layerId==overlay){covers.push_back(&e);}
  else if(e.layerId==root.children[0]){auto lowerValid=validateOriginalWhitePaint(e,s,raw.value(),stop);if(!lowerValid)return lowerValid.error();}
  else if(!existing||e.layerId!=colorLayer)return error(ErrorCode::UnsupportedFeature,"derived-fill.other-paint");
 }
 if(covers.empty())return error(ErrorCode::InvalidInput,"derived-fill.missing-overlay");
 for(const auto* cover:covers){auto coverValid=validateBucketEvent(*cover);if(!coverValid)return coverValid.error();
 if(cover->color.activeChannels!=3||cover->color.profileId!=s.spec.colorSpec.profileId||cover->color.alpha!=1||cover->color.channels[0]!=1||cover->color.channels[1]!=1||cover->color.channels[2]!=1)return error(ErrorCode::UnsupportedFeature,"derived-fill.overlay-not-white");}
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
     if(cleared[pos])return error(ErrorCode::InvalidInput,"derived-fill.overlapping-cover");
     if(col==0||row==0||col+1==w||row+1==h)return error(ErrorCode::InvalidInput,"derived-fill.cover-touches-ink-edge");
     for(unsigned yy=row-1;yy<=row+1;++yy)for(unsigned xx=col-1;xx<=col+1;++xx){const auto at=(std::size_t(yy)*w+xx)*4;if(!raw.value()[at+3]||raw.value()[at]==255)return error(ErrorCode::InvalidInput,"derived-fill.cover-touches-ink-edge");}
     cleared[pos]=1;std::fill_n(reference.bytes.begin()+pos*4,4,255);
    }else if(!cleared[pos])return error(ErrorCode::UnsupportedFeature,"derived-fill.color-outside-cover");
   }
  }return {};
 };
 for(const auto* cover:covers){auto covered=walk(*cover,true);if(!covered)return covered.error();}
 for(const auto& e:*s.eventIndex)if(e.layerId==colorLayer){auto checked=walk(e,false);if(!checked)return checked.error();}
 return HollowDerivedReference{std::move(reference),std::move(cleared)};
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"derived-fill.allocation");}
inline Result<void> validateHollowColorDomain(const StrokeEvent& e,
 const HollowDerivedReference& ref,std::stop_token stop={}) {
 auto checked=validateBucketEvent(e);if(!checked)return checked.error();
 const auto w=ref.reference.width,h=ref.reference.height;
 for(std::size_t i=0;i<e.geometry.size();i+=2){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"derived-fill.cancelled");
  const auto& a=e.geometry[i];const auto& b=e.geometry[i+1];
  if(a.x<0||a.y<0||b.x>double(w)||a.y>=double(h))return error(ErrorCode::InvalidInput,"derived-fill.run-bounds");
  for(auto col=std::uint32_t(a.x);col<std::uint32_t(b.x);++col)
   if(!ref.cleared[std::size_t(a.y)*w+col])return error(ErrorCode::UnsupportedFeature,"derived-fill.color-outside-cover");
 }
 return {};
}
}
