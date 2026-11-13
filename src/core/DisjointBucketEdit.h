#pragma once
#include "Document.h"
#include "BucketEvent.h"
#include "CubicInkEvent.h"
#include "FreehandEvent.h"
#include <algorithm>
#include <stop_token>
namespace drawborad {
// Dedicated geometry-edit exception only. Never classifies a bucket as an
// independent native stroke. Full old/new event AABBs deliberately over-reject.
inline Result<void> validateDisjointBucketEdit(const DocumentSnapshot&s,std::size_t index,const StrokeEvent& proposed,std::stop_token stop={}) {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");
 if(!s.nodes||!s.eventIndex||index>=s.eventIndex->size())return error(ErrorCode::InvalidInput,"stroke-edit.missing");
 bool bucket=false;for(std::size_t i=index+1;i<s.eventIndex->size();++i){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");bucket|=s.eventIndex->at(i).engineVersion==8;}
 if(!bucket)return {}; // Preserve the preexisting no-bucket editing route.
 if(!s.fillOperations.empty())return error(ErrorCode::UnsupportedFeature,"stroke-edit.recorded-fill-barrier");
 if(s.eventIndex->size()>8192||s.nodes->size()>4096)return error(ErrorCode::BudgetExceeded,"stroke-edit.document-budget");
 std::size_t points=0;for(const auto&e:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");if(e.geometry.size()>1000000-points)return error(ErrorCode::BudgetExceeded,"stroke-edit.points");points+=e.geometry.size();}
 // First version intentionally rejects all non-plain stacks, even unrelated
 // raster sources. Visibility/opacity do not exempt any later bucket.
 for(const auto&[id,n]:*s.nodes){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");if(!n.stage.empty()||n.transform!=LayerTransform{}||!n.maskId.empty()||n.blend!=BlendMode::Normal||!n.sourceResource.empty()||!n.content.empty()||n.sourceKind!=SourceKind::Reconstructed)return error(ErrorCode::UnsupportedFeature,"stroke-edit.bucket-stack");}
 const auto& old=s.eventIndex->at(index);
 if(old.engineVersion!=proposed.engineVersion||(old.engineVersion!=19&&old.engineVersion!=23&&old.engineVersion!=24))return error(ErrorCode::UnsupportedFeature,"stroke-edit.bucket-engine");
 auto validate=[](const StrokeEvent&e)->Result<void>{if(e.engineVersion==19)return validateSmoothInkEvent(e);if(e.engineVersion==23)return validateCubicInkEvent(e);return validateCubicPathEvent(e);};
 auto valid=validate(old);if(!valid)return valid.error();valid=validate(proposed);if(!valid)return valid.error();
 struct Box{double x0,y0,x1,y1;};
 auto bounds=[&](const StrokeEvent&e)->Result<Box>{Box b{e.geometry.front().x,e.geometry.front().y,e.geometry.front().x,e.geometry.front().y};for(const auto&p:e.geometry){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9)return error(ErrorCode::InvalidInput,"stroke-edit.bucket-coordinates");b.x0=std::min(b.x0,p.x);b.y0=std::min(b.y0,p.y);b.x1=std::max(b.x1,p.x);b.y1=std::max(b.y1,p.y);}const auto pad=e.style.width*.5+2;b.x0-=pad;b.y0-=pad;b.x1+=pad;b.y1+=pad;return b;};
 auto a=bounds(old),b=bounds(proposed);if(!a)return a.error();if(!b)return b.error();
 // Engine19 is capsule coverage with radius <= width/2 and .5px AA.
 // 23/24 de Casteljau samples are inside the control hull, rendered as19.
 // +2 covers AA/numeric margins without depending on layer bounds/clipping.
 for(std::size_t i=index+1;i<s.eventIndex->size();++i){const auto&e=s.eventIndex->at(i);if(e.engineVersion!=8)continue;valid=validateBucketEvent(e);if(!valid)return valid.error();for(std::size_t j=0;j<e.geometry.size();j+=2){if(stop.stop_requested())return error(ErrorCode::Cancelled,"stroke-edit.cancelled");const double x0=e.geometry[j].x-2,x1=e.geometry[j+1].x+2,y0=e.geometry[j].y-2,y1=e.geometry[j].y+3;auto disjoint=[&](Box q){return q.x1<x0||q.x0>x1||q.y1<y0||q.y0>y1;};if(!disjoint(a.value())||!disjoint(b.value()))return error(ErrorCode::UnsupportedFeature,"stroke-edit.bucket-footprint");}}
 return {};
}
}
