#include "brush/DerivedHollowFill.h"
#include "brush/HollowInkCommand.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest req;req.dimensions={32,32,Unit::Px,300};req.transparent=false;req.paperColor.channels={1,1,1,0};req.paperColor.alpha=1;auto spec=validateDocument(req,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(1));assert(doc);
 const auto lower=Id::fromNumber(2),source=Id::fromNumber(3),overlay=Id::fromNumber(4),colorLayer=Id::fromNumber(5);
 auto add=addFillLayerCommand(doc.value().snapshot(),lower,true,true);assert(add&&doc.value().apply(add.value(),0));
 Bytes raw(32*32*4,255);FillMask selected;for(unsigned y=4;y<28;y++){selected.runs.push_back({y,4,24});selected.pixelCount+=24;for(unsigned x=4;x<28;x++){const auto p=(y*32+x)*4;raw[p]=raw[p+1]=raw[p+2]=0;}}
 auto line=makeLineartRasterCommand(raw,{'s'},32,32,source);assert(line&&doc.value().apply(line.value(),doc.value().snapshot().revision));auto hollow=hollowInkOverlayCommand(doc.value().snapshot(),source,overlay,Id::fromNumber(6),selected,1);assert(hollow&&doc.value().apply(hollow.value(),doc.value().snapshot().revision));const auto baseline=doc.value().snapshot();
 ColorValue red;red.channels={1,0,0,0};red.alpha=1;auto fill=derivedHollowFillCommand(baseline,source,overlay,colorLayer,Id::fromNumber(7),10,10,red);assert(fill&&fill.value().operations.size()==2);assert(doc.value().apply(fill.value(),baseline.revision));const auto colored=doc.value().snapshot();
 assert(colored.eventIndex->front()==baseline.eventIndex->front());for(auto&[id,r]:*baseline.resources)assert(colored.resources->at(id).bytes==r.bytes&&sha256(*r.bytes)==id);
 auto frame=renderMergedDocument(colored,{colored.revision});assert(frame);auto& p=frame.value().merged.bytes;assert(p[(10*32+10)*4]==255&&p[(10*32+10)*4+1]==0);assert(p[(4*32+10)*4]==0);
 ColorValue blue=red;blue.channels={0,0,1,0};auto again=derivedHollowFillCommand(colored,source,overlay,colorLayer,Id::fromNumber(8),10,10,blue);assert(again&&again.value().operations.size()==1);const auto& a=std::get<Paint>(fill.value().operations.back()).event.geometry;const auto& b=std::get<Paint>(again.value().operations.back()).event.geometry;assert(a.size()==b.size());for(std::size_t i=0;i<a.size();++i)assert(a[i].x==b[i].x&&a[i].y==b[i].y);
 assert(doc.value().apply(again.value(),colored.revision));auto twice=doc.value().snapshot();assert(doc.value().undo()&&doc.value().snapshot().sameContent(colored));assert(doc.value().undo()&&doc.value().snapshot().sameContent(baseline));assert(doc.value().redo()&&doc.value().snapshot().sameContent(colored));assert(doc.value().redo()&&doc.value().snapshot().sameContent(twice));
 auto path=std::filesystem::temp_directory_path()/("derived-fill-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");ProjectStore store;assert(store.save(path,twice));auto reopened=store.recover(path);assert(reopened&&reopened.value().snapshot.sameContent(twice));assert(derivedHollowFillCommand(reopened.value().snapshot,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));std::filesystem::remove(path);
 assert(!derivedHollowFillCommand(baseline,source,overlay,colorLayer,Id::fromNumber(9),1,1,red));assert(!derivedHollowFillCommand(baseline,source,overlay,colorLayer,Id::fromNumber(9),4,10,red));
 std::stop_source stop;stop.request_stop();auto cancelled=derivedHollowFillCommand(baseline,source,overlay,colorLayer,Id::fromNumber(9),10,10,red,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled);
 auto rejectNodes=[&](auto change){auto s=baseline;auto ns=std::make_shared<NodeMap>(*s.nodes);s.nodes=ns;change(*ns);assert(!derivedHollowFillCommand(s,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));};
 rejectNodes([&](auto& ns){ns.at(overlay).opacity=.5;});rejectNodes([&](auto& ns){ns.at(overlay).visible=false;});rejectNodes([&](auto& ns){ns.at(overlay).transform.affine[4]=1;});rejectNodes([&](auto& ns){ns.at(source).locked=false;});rejectNodes([&](auto& ns){ns.at(Document::rootId()).opacity=.5;});
 auto altered=baseline;auto es=std::make_shared<EventList>(*baseline.eventIndex);altered.eventIndex=es;es->front().color.channels={0,0,0,0};assert(!derivedHollowFillCommand(altered,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 *es=*baseline.eventIndex;es->front().geometry[0].x=4;assert(!derivedHollowFillCommand(altered,source,overlay,colorLayer,Id::fromNumber(9),10,10,red)); // cover erases original ink edge
 *es=*baseline.eventIndex;auto other=es->front();other.id=Id::fromNumber(30);other.layerId=lower;es->push_back(other);assert(!derivedHollowFillCommand(altered,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 *es=*baseline.eventIndex;auto duplicate=es->front();duplicate.id=Id::fromNumber(31);es->push_back(duplicate);assert(!derivedHollowFillCommand(altered,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 *es=*baseline.eventIndex;es->front().geometry[0].x=5.5;assert(!derivedHollowFillCommand(altered,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 auto badColor=colored;auto colorEvents=std::make_shared<EventList>(*colored.eventIndex);badColor.eventIndex=colorEvents;colorEvents->back().geometry[0].x=0;assert(!derivedHollowFillCommand(badColor,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 auto over=baseline;over.spec.pixelSize={8192,8192};assert(!derivedHollowFillCommand(over,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 // Layer names/stages are not a provenance pass: white pixels and geometry decide.
 auto renamed=baseline;auto ns=std::make_shared<NodeMap>(*baseline.nodes);renamed.nodes=ns;ns->at(overlay).name="not a magic provenance label";assert(derivedHollowFillCommand(renamed,source,overlay,colorLayer,Id::fromNumber(9),10,10,red));
 // Two independent ink components: fill A, append hollow B above the same
 // original, then fill B. No lower-layer recoloring or flattening of A.
 {
  NewDocumentRequest r=req;r.dimensions={64,64,Unit::Px,300};auto sp=validateDocument(r,{});assert(sp);
  auto created=Document::create(sp.value(),Id::fromNumber(80));assert(created);auto& d=created.value();
  auto lowerCommand=addFillLayerCommand(d.snapshot(),lower,true,true);assert(lowerCommand&&d.apply(lowerCommand.value(),0));
  Bytes pixels(64*64*4,255);FillMask regionA,regionB;
  for(unsigned y=4;y<24;y++)for(unsigned component=0;component<2;component++){
   const unsigned left=component?36:4;auto& region=component?regionB:regionA;
   region.runs.push_back({y,left,20});region.pixelCount+=20;
   for(unsigned x=left;x<left+20;x++){const auto k=(y*64+x)*4;pixels[k]=pixels[k+1]=pixels[k+2]=0;}
  }
  auto imported=makeLineartRasterCommand(pixels,{'t'},64,64,source);assert(imported&&d.apply(imported.value(),d.snapshot().revision));const auto original=d.snapshot();
  auto aHollow=hollowInkOverlayCommand(d.snapshot(),source,overlay,Id::fromNumber(81),regionA,1);assert(aHollow&&d.apply(aHollow.value(),d.snapshot().revision));
  auto aColor=derivedHollowFillCommand(d.snapshot(),source,overlay,colorLayer,Id::fromNumber(82),10,10,red);assert(aColor&&d.apply(aColor.value(),d.snapshot().revision));const auto aDone=d.snapshot();
  auto bHollow=hollowInkOverlayCommand(aDone,source,overlay,Id::fromNumber(83),regionB,1);assert(bHollow&&bHollow.value().operations.size()==1);assert(d.apply(bHollow.value(),aDone.revision));const auto bWhite=d.snapshot();
  assert(bWhite.nodes->size()==aDone.nodes->size());for(std::size_t i=0;i<aDone.eventIndex->size();i++)assert(bWhite.eventIndex->at(i)==aDone.eventIndex->at(i));
  auto repeated=hollowInkOverlayCommand(bWhite,source,overlay,Id::fromNumber(84),regionA,1);assert(!repeated&&repeated.error().messageKey=="hollow.already-cleared");
  assert(!hollowInkOverlayCommand(bWhite,source,overlay,Id::fromNumber(84),regionB,1));
  auto bColor=derivedHollowFillCommand(bWhite,source,overlay,colorLayer,Id::fromNumber(84),42,10,blue);assert(bColor&&d.apply(bColor.value(),bWhite.revision));const auto both=d.snapshot();
  auto firstFrame=renderMergedDocument(aDone,{aDone.revision});auto bothFrame=renderMergedDocument(both,{both.revision});assert(firstFrame&&bothFrame);
  for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
   const auto k=(y*64+x)*4;const bool bInterior=x>36&&x<55&&y>4&&y<23;
   if(bInterior){assert(bothFrame.value().merged.bytes[k]==0&&bothFrame.value().merged.bytes[k+1]==0&&bothFrame.value().merged.bytes[k+2]==255);}
   else for(unsigned c=0;c<4;c++)assert(bothFrame.value().merged.bytes[k+c]==firstFrame.value().merged.bytes[k+c]);
  }
  for(const auto&[id,res]:*original.resources)assert(both.resources->at(id).bytes==res.bytes&&sha256(*res.bytes)==id);
  assert(d.undo()&&d.snapshot().sameContent(bWhite));assert(d.undo()&&d.snapshot().sameContent(aDone));assert(d.redo()&&d.redo()&&d.snapshot().sameContent(both));
  auto file=std::filesystem::temp_directory_path()/("multi-hollow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");assert(store.save(file,both));auto recovery=store.recover(file);assert(recovery&&recovery.value().snapshot.sameContent(both));
  assert(derivedHollowFillCommand(recovery.value().snapshot,source,overlay,colorLayer,Id::fromNumber(85),10,10,blue));assert(derivedHollowFillCommand(recovery.value().snapshot,source,overlay,colorLayer,Id::fromNumber(86),42,10,red));std::filesystem::remove(file);
  // Validate *all* covers, not just the first; corrupt second cover is refused.
  auto corrupted=both;auto events=std::make_shared<EventList>(*both.eventIndex);corrupted.eventIndex=events;events->at(2).color.channels={0,0,0,0};assert(!derivedHollowFillCommand(corrupted,source,overlay,colorLayer,Id::fromNumber(87),10,10,red));assert(!hollowInkOverlayCommand(corrupted,source,overlay,Id::fromNumber(87),regionB,1));
  *events=*both.eventIndex;for(auto& point:events->at(2).geometry)point.x-=32;auto overlap=derivedHollowFillCommand(corrupted,source,overlay,colorLayer,Id::fromNumber(87),10,10,red);assert(!overlap&&overlap.error().messageKey=="derived-fill.overlapping-cover");
  std::stop_source cancelledAppend;cancelledAppend.request_stop();assert(!hollowInkOverlayCommand(both,source,overlay,Id::fromNumber(87),regionB,1,cancelledAppend.get_token()));
  // The no-color three-leaf stack also supports successive disjoint hollows.
  auto noColor=Document::fromSnapshot(original);assert(noColor);assert(noColor.value().apply(aHollow.value(),original.revision));auto next=hollowInkOverlayCommand(noColor.value().snapshot(),source,overlay,Id::fromNumber(88),regionB,1);assert(next&&noColor.value().apply(next.value(),noColor.value().snapshot().revision));assert(derivedHollowFillCommand(noColor.value().snapshot(),source,overlay,colorLayer,Id::fromNumber(89),42,10,red));
 }
 std::cout<<"PASS derived hollow fill: atomic new layer, source-only repeated reference, exact old resources/cover, undo/redo/save/reopen, stack/color/edge/cancel guards\n";
}
