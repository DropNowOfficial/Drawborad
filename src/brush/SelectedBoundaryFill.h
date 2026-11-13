#pragma once
#include "MixedLineartStack.h"
#include "ui/MergedDocument.h"
namespace drawborad {
// Explicitly chosen finishing layer only. Sample its rendered alpha, never its
// RGB or any other detail layer. Threshold is 128/255, with no gap closing.
inline Result<Command> selectedBoundaryFillCommand(const DocumentSnapshot& full,NodeId selected,Id event,
 std::uint32_t x,std::uint32_t y,ColorValue color,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"selected-boundary.cancelled");
 const auto w=full.spec.pixelSize.width,h=full.spec.pixelSize.height;
 if(!w||!h||w>8192||h>8192||w*h>20000000)return error(ErrorCode::BudgetExceeded,"selected-boundary.canvas");
 const auto n=w*h;constexpr std::uint64_t limit=256ull*1024*1024;
 // Phased incremental accounting, not RSS: render plan, 8N combining two
 // RGBA buffers, then 9N fill plus bounded runs/geometry. Snapshot/history,
 // validation copies and the caller's later preview are excluded.
 const auto fillScratch=9*n+2*FillBudget{}.maxRuns*sizeof(FillRun)+4*FillBudget{}.maxRuns*sizeof(InputSample);
 if(fillScratch>limit)return error(ErrorCode::BudgetExceeded,"selected-boundary.scratch");
 auto projected=projectControlledLineart(full,stop);if(!projected)return projected.error();
 const auto& view=projected.value();const auto& controlled=view.controlled;
 if(std::find(view.finishing.begin(),view.finishing.end(),selected)==view.finishing.end()||!full.nodes->at(selected).visible)return error(ErrorCode::InvalidInput,"selected-boundary.visible-finishing-required");
 const auto& children=controlled.nodes->at(Document::rootId()).children;const auto source=children[1],target=children[0];
 if(x>=w||y>=h)return error(ErrorCode::InvalidInput,"selected-boundary.seed");
 auto raw=lineartRasterPixels(controlled,source);if(!raw)return raw.error();const auto seed=(std::size_t(y)*w+x)*4;
 if(raw.value()[seed+3]&&raw.value()[seed]!=255)return error(ErrorCode::InvalidInput,"selected-boundary.original-white-only");
 std::vector<std::uint32_t> barriers;
 if(children.size()>=3){auto ref=hollowDerivedReference(controlled,source,children[2],children.size()==4?children[3]:NodeId{},stop);if(!ref)return ref.error();barriers=std::move(ref.value().barriers);}
 const auto barrierBytes=std::uint64_t(barriers.capacity())*sizeof(std::uint32_t);
 if(barrierBytes>limit||fillScratch>limit-barrierBytes||8*n>limit-barrierBytes)return error(ErrorCode::BudgetExceeded,"selected-boundary.barrier-scratch");
 auto isolated=full;auto nodes=std::make_shared<NodeMap>();auto root=full.nodes->at(Document::rootId());root.children={selected};nodes->emplace(root.id,std::move(root));nodes->emplace(selected,full.nodes->at(selected));auto events=std::make_shared<EventList>();for(const auto& e:*full.eventIndex)if(e.layerId==selected)events->push_back(e);isolated.nodes=nodes;isolated.eventIndex=events;isolated.spec.background.transparent=true;
 MergedBudget budget;budget.maxBytes=limit-barrierBytes;auto rendered=renderMergedDocument(isolated,{isolated.revision},budget,stop);if(!rendered)return rendered.error();
 const auto& alpha=rendered.value().merged;
 if(alpha.width!=w||alpha.height!=h||alpha.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::CorruptData,"selected-boundary.render-layout");
 if(alpha.bytes[std::size_t(y)*alpha.rowStride+x*4+3]>=128||std::binary_search(barriers.begin(),barriers.end(),std::uint32_t(y*w+x)))return error(ErrorCode::InvalidInput,"selected-boundary.seed-on-barrier");
 TileData reference;reference.width=std::uint32_t(w);reference.height=std::uint32_t(h);reference.rowStride=w*4;reference.format=full.spec.colorSpec.format;reference.profileId=full.spec.colorSpec.profileId;reference.bytes.resize(n*4);
 for(std::size_t i=0;i<n;i++){if(i%16384==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"selected-boundary.cancelled");std::copy_n(raw.value().begin()+i*4,4,reference.bytes.begin()+i*4);if(alpha.bytes[(i/w)*alpha.rowStride+(i%w)*4+3]>=128){reference.bytes[i*4]=reference.bytes[i*4+1]=reference.bytes[i*4+2]=0;reference.bytes[i*4+3]=255;}}
 for(auto pos:barriers){const auto k=std::size_t(pos)*4;reference.bytes[k]=reference.bytes[k+1]=reference.bytes[k+2]=0;reference.bytes[k+3]=255;}
 // Release rendered RGBA before the bucket allocates its RGBA/visited buffers.
 Bytes{}.swap(rendered.value().merged.bytes);
 auto command=bucketFillCommand(controlled,target,event,reference,x,y,0,color,stop);if(!command)return command.error();
 for(const auto& op:command.value().operations){const auto* paint=std::get_if<Paint>(&op);if(!paint)return error(ErrorCode::InvalidInput,"selected-boundary.operation");auto valid=validateOriginalWhitePaint(paint->event,controlled,raw.value(),stop);if(!valid)return valid.error();}
 Bytes{}.swap(reference.bytes);
 return restoreControlledCommand(full,view,std::move(command.value()),stop);
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"selected-boundary.allocation");}
}
