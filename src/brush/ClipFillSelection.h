#pragma once
#include "FloodFill.h"
#include "core/Types.h"
#include <algorithm>
namespace drawborad {
// Exact half-open integer rectangle intersection. No expansion, topology repair,
// resampling or tiny-island deletion. Rect is integral; UI callers must reject
// fractional/nonfinite input before converting to Rect. Scratch is output runs
// only (<=65536*sizeof(FillRun)), excluding the caller-owned mask.
inline Result<FillMask> clipFillSelection(const FillMask& mask,
 std::uint32_t w,std::uint32_t h,Rect selection,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-fill.cancelled");
 if(!w||!h||w>8192||h>8192||std::uint64_t(w)*h>20000000||mask.runs.size()>FillBudget{}.maxRuns)
  return error(ErrorCode::BudgetExceeded,"clip-fill.budget");
 // Subtraction after range checks avoids overflow on adversarial Rect widths.
 if(selection.x<0||selection.y<0||std::uint64_t(selection.x)>=w||std::uint64_t(selection.y)>=h||
    !selection.width||!selection.height||selection.width>w-std::uint64_t(selection.x)||selection.height>h-std::uint64_t(selection.y))
  return error(ErrorCode::InvalidInput,"clip-fill.rectangle");
 if(mask.runs.empty()||!mask.pixelCount)return error(ErrorCode::InvalidInput,"clip-fill.empty-mask");
 const auto left=std::uint32_t(selection.x),top=std::uint32_t(selection.y);
 const auto right=left+std::uint32_t(selection.width),bottom=top+std::uint32_t(selection.height);
 FillMask out;out.runs.reserve(mask.runs.size());std::uint64_t count=0;FillRun previous{};bool first=true;
 for(const auto& r:mask.runs){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"clip-fill.cancelled");
  if(!r.length||r.y>=h||r.x>=w||r.length>w-r.x||
     (!first&&(r.y<previous.y||(r.y==previous.y&&r.x<previous.x+previous.length))))
   return error(ErrorCode::InvalidInput,"clip-fill.runs");
  count+=r.length;previous=r;first=false;
  if(r.y<top||r.y>=bottom)continue;
  const auto a=std::max(left,r.x),b=std::min(right,r.x+r.length);
  if(a<b){out.runs.push_back({r.y,a,b-a});out.pixelCount+=b-a;}
 }
 if(count!=mask.pixelCount)return error(ErrorCode::InvalidInput,"clip-fill.count");
 if(!out.pixelCount)return error(ErrorCode::InvalidInput,"clip-fill.empty-intersection");
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"clip-fill.allocation");}
}
