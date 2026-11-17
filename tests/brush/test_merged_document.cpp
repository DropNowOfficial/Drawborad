#include "ui/MergedDocument.h"
#include "ui/PaintedDocument.h"
#include "core/Commands.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
DocumentSnapshot scene(unsigned w,unsigned h){
 NewDocumentRequest req;req.dimensions={double(w),double(h),Unit::Px,300};
 auto s=validateDocument(req,{});assert(s);auto d=Document::create(s.value(),Id::fromNumber(99));assert(d);
 for(unsigned i=2;i<4;i++){
  LayerNode n;n.id=Id::fromNumber(i);n.parent=Document::rootId();n.opacity=i==3?.4:1;
  assert(d.value().apply(Command(AddNode{n}),d.value().snapshot().revision));
  StrokeEvent e;e.id=Id::fromNumber(i+10);e.layerId=n.id;e.engineVersion=8;e.toolId="bucket";e.parentRevision=d.value().snapshot().revision;
  e.color.channels={i==2?1.:0.,0.,i==3?1.:0.,0.};
  for(unsigned y=0;y<h;y++)e.geometry.insert(e.geometry.end(),{{0.,double(y),1,{}, {},0,0,0},{double(w),double(y),1,{}, {},0,0,0}});
  assert(d.value().apply(Command(Paint{e,{0,0,w,h}}),e.parentRevision));
 }
 return d.value().snapshot();
}
void oracle(const DocumentSnapshot& s){
 auto old=renderPaintedDocument(s,{s.revision});auto fresh=renderMergedDocument(s,{s.revision});
 assert(old&&fresh&&old.value().merged.bytes==fresh.value().merged.bytes);
}
void corrupt(const DocumentSnapshot& s){
 auto p=planMergedDocument(s);assert(!p && p.error().code==ErrorCode::CorruptData);
 auto r=renderMergedDocument(s,{s.revision});assert(!r && r.error().code==ErrorCode::CorruptData);
}
int main(){
 corrupt(DocumentSnapshot{});
 auto broken=scene(8,8);broken.nodes.reset();corrupt(broken);
 broken=scene(8,8);broken.eventIndex.reset();corrupt(broken);
 broken=scene(8,8);broken.resources.reset();corrupt(broken);

 auto many=scene(128,128);auto manyEvents=std::make_shared<EventList>();
 for(unsigned i=0;i<4418;i++){
  auto e=many.eventIndex->front();e.id=Id::fromNumber(1000+i);e.engineVersion=2;e.toolId="fineliner";
  e.geometry={{64,64,1,{}, {},0,0,0}};manyEvents->push_back(e);
 }
 many.eventIndex=manyEvents;auto manyPlan=planMergedDocument(many);assert(manyPlan);
 auto manyPixels=renderMergedDocument(many,{many.revision});assert(manyPixels);
 assert(manyPixels.value().statistics.rasterizedEvents==4418);
 assert(manyPlan.value().coveragePixels==4418ull*128*128);
 auto excessive=many;auto excessiveEvents=std::make_shared<EventList>(*manyEvents);
 for(auto& e:*excessiveEvents){e.style.width=8192;}
 excessive.eventIndex=excessiveEvents;assert(!planMergedDocument(excessive));
 auto overLimit=std::make_shared<EventList>(*manyEvents);
 while(overLimit->size()<8193){auto e=overLimit->front();e.id=Id::fromNumber(1000+overLimit->size());overLimit->push_back(e);}
 excessive.eventIndex=overLimit;assert(planMergedDocument(excessive));
 assert(renderMergedDocument(excessive,{excessive.revision}));
 while(overLimit->size()<65537){auto e=overLimit->front();e.id=Id::fromNumber(1000+overLimit->size());overLimit->push_back(e);}
 excessive.eventIndex=overLimit;assert(!planMergedDocument(excessive));
 auto small=scene(515,519);
 {auto dry=small;auto strokes=std::make_shared<EventList>(*small.eventIndex);auto& de=strokes->front();
 de.engineVersion=12;de.toolId="dry-ink";de.style.width=12;de.style.hardness=1;de.geometry={{20,20,1,{},{},0,0,0}};
 dry.eventIndex=strokes;auto dp=planMergedDocument(dry);auto bp=planMergedDocument(small);assert(dp&&bp);
 assert(dp.value().tileScratchBytes==512ull*512*46);
 assert(bp.value().tileScratchBytes==512ull*512*14);
 MergedBudget tight;tight.maxBytes=dp.value().estimatedBytes-1;auto rejected=planMergedDocument(dry,tight);assert(!rejected&&rejected.error().messageKey=="merged.memory-limit");}

 auto brush=small;auto events=std::make_shared<EventList>(*small.eventIndex);
 auto& e=events->front();e.engineVersion=2;e.toolId="fineliner";e.style.width=7;e.style.hardness=.6;
 e.geometry={{1,1,.3,{}, {},0,0,0},{514,518,1,{}, {},0,0,0}};brush.eventIndex=events;oracle(brush);
 auto limitedOld=renderPaintedDocument(brush,{brush.revision,1});auto limitedNew=renderMergedDocument(brush,{brush.revision,1});
 assert(limitedOld&&limitedNew&&limitedOld.value().merged.bytes==limitedNew.value().merged.bytes);
 auto invalid=small;invalid.spec.pixelSize={UINT64_MAX,UINT64_MAX};assert(!planMergedDocument(invalid));

 auto masked=small;auto nodes=std::make_shared<NodeMap>(*small.nodes);auto resources=std::make_shared<ResourceMap>(*small.resources);
 auto mask=std::make_shared<Bytes>(515*519,128);(*mask)[0]=0;(*mask)[512]=255;(*mask).back()=255;
 Resource resource;resource.id=sha256(*mask);resource.kind=ResourceKind::Mask;resource.bytes=mask;
 (*resources)[resource.id]=resource;nodes->at(Id::fromNumber(3)).maskId=resource.id;
 masked.nodes=nodes;masked.resources=resources;oracle(masked);
 nodes->at(Id::fromNumber(3)).visible=false;oracle(masked);
 masked.spec.background.transparent=false;masked.spec.background.paperColor.channels={.3,.6,.9,0};oracle(masked);
 nodes->at(Id::fromNumber(3)).visible=true;
 nodes->at(Document::rootId()).maskId=resource.id;
 assert(!renderMergedDocument(masked,{masked.revision})); // Soft ancestor mask is unsupported.
 std::fill(mask->begin(),mask->end(),255);(*mask)[0]=0; // Fresh content-addressed binary mask.
 resources->clear();resource.id=sha256(*mask);resource.bytes=mask;(*resources)[resource.id]=resource;
 nodes->at(Document::rootId()).maskId=resource.id;nodes->at(Id::fromNumber(3)).maskId=resource.id;
 oracle(masked);
 nodes->at(Id::fromNumber(3)).transform.affine[4]=1;assert(!planMergedDocument(masked));

 for(auto wh:{std::pair{8u,7u},std::pair{515u,519u}}){
  auto s=scene(wh.first,wh.second);auto old=renderPaintedDocument(s,{s.revision});auto fresh=renderMergedDocument(s,{s.revision});
  assert(old&&fresh&&old.value().merged.bytes==fresh.value().merged.bytes);
 }
 auto extra=small;auto extraNodes=std::make_shared<NodeMap>(*small.nodes);
 for(unsigned i=100;i<120;i++){LayerNode n;n.id=Id::fromNumber(i);n.parent=Document::rootId();(*extraNodes)[n.id]=n;extraNodes->at(Document::rootId()).children.push_back(n.id);}
 extra.nodes=extraNodes;auto p0=planMergedDocument(small),p1=planMergedDocument(extra);assert(p0&&p1);
 assert(p0.value().mergedBytes==p1.value().mergedBytes && p0.value().tileScratchBytes==p1.value().tileScratchBytes);
 assert(p1.value().estimatedBytes-p0.value().estimatedBytes==20*128);
 MergedBudget b0;b0.maxIndexEntries=0;assert(!planMergedDocument(small,b0));
 b0={};b0.maxCoveragePixels=0;assert(!planMergedDocument(small,b0));
 b0={};b0.maxGeometryPoints=0;assert(!planMergedDocument(small,b0));
 b0={};b0.maxRasterWork=0;assert(!planMergedDocument(small,b0));
 auto scanHeavy=scene(1725,2625);
 auto heavyNodes=std::make_shared<NodeMap>(*scanHeavy.nodes);
 for(unsigned i=100;i<130;i++){LayerNode n;n.id=Id::fromNumber(i);n.parent=Document::rootId();(*heavyNodes)[n.id]=n;heavyNodes->at(Document::rootId()).children.push_back(n.id);}
 auto heavyResources=std::make_shared<ResourceMap>();Resource binary;
 binary.kind=ResourceKind::Mask;binary.bytes=std::make_shared<Bytes>(1725*2625,255);binary.id=sha256(*binary.bytes);
 (*heavyResources)[binary.id]=binary;heavyNodes->at(Document::rootId()).maskId=binary.id;
 scanHeavy.nodes=heavyNodes;scanHeavy.resources=heavyResources;
 auto rejected=planMergedDocument(scanHeavy);assert(!rejected && rejected.error().code==ErrorCode::BudgetExceeded);
 auto s=scene(1725,2625);auto p=planMergedDocument(s);assert(p&&p.value().mergedBytes==18112500);
 MergedBudget tight;tight.maxBytes=p.value().estimatedBytes;assert(planMergedDocument(s,tight));tight.maxBytes--;assert(!planMergedDocument(s,tight));
 auto out=renderMergedDocument(s,{s.revision});assert(out&&out.value().merged.width==1725&&out.value().merged.height==2625);
 assert(out.value().statistics.reconstructionCalls==48);
 const auto& b=out.value().merged.bytes;for(std::size_t i=0;i<b.size();i+=4){assert(b[i]==153&&b[i+1]==0&&b[i+2]==102&&b[i+3]==255);}
 std::stop_source stop;stop.request_stop();assert(!renderMergedDocument(s,{s.revision},{},stop.get_token()));
 assert(!renderMergedDocument(s,{s.revision+1}));
 std::cout<<"merged document tests passed\n";
}
