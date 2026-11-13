#include "brush/CloseLineartGap.h"
#include "brush/DerivedHollowFill.h"
#include "brush/HollowInkCommand.h"
#include "brush/OriginalLineartFill.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest request;request.dimensions={32,32,Unit::Px,300};request.transparent=false;request.paperColor.channels={1,1,1,0};request.paperColor.alpha=1;auto spec=validateDocument(request,{});assert(spec);auto created=Document::create(spec.value(),Id::fromNumber(1));assert(created);auto& doc=created.value();
 const auto lower=Id::fromNumber(2),source=Id::fromNumber(3),overlay=Id::fromNumber(4),upper=Id::fromNumber(5);auto add=addFillLayerCommand(doc.snapshot(),lower,true,true);assert(add&&doc.apply(add.value(),0));
 Bytes raw(32*32*4,255);auto ink=[&](unsigned x,unsigned y){const auto k=(y*32+x)*4;raw[k]=raw[k+1]=raw[k+2]=0;};
 for(unsigned n=6;n<=25;n++){ink(6,n);ink(25,n);ink(n,25);if(n<14||n>17)ink(n,6);}
 FillMask block;for(unsigned y=10;y<=14;y++)for(unsigned x=10;x<=14;x++){ink(x,y);if(x==10){block.runs.push_back({y,10,5});block.pixelCount+=5;}}
 for(unsigned y=10;y<=14;y++)for(unsigned x=18;x<=22;x++)ink(x,y);
 auto imported=makeLineartRasterCommand(raw,{'g'},32,32,source);assert(imported&&doc.apply(imported.value(),doc.snapshot().revision));const auto original=doc.snapshot();
 ColorValue red;red.channels={1,0,0,0};red.alpha=1;auto flood=originalLineartFillCommand(original,source,lower,Id::fromNumber(6),8,8,red);assert(flood&&doc.apply(flood.value(),original.revision));const auto leaked=doc.snapshot();auto leakedFrame=renderMergedDocument(leaked,{leaked.revision});assert(leakedFrame&&leakedFrame.value().merged.bytes[(1*32+1)*4]==255&&leakedFrame.value().merged.bytes[(8*32+8)*4]==255);
 auto diagonal=closeLineartGapCommand(leaked,source,overlay,Id::fromNumber(7),10,10,22,14);auto backwards=closeLineartGapCommand(leaked,source,overlay,Id::fromNumber(7),22,14,10,10);assert(diagonal&&backwards);assert(std::get<Paint>(diagonal.value().operations.back()).event==std::get<Paint>(backwards.value().operations.back()).event);
 auto close=closeLineartGapCommand(leaked,source,overlay,Id::fromNumber(7),13,6,18,6);assert(close&&close.value().operations.size()==2&&doc.apply(close.value(),leaked.revision));const auto closed=doc.snapshot();assert(closed.eventIndex->front()==leaked.eventIndex->front());assert(!closeLineartGapCommand(closed,source,overlay,Id::fromNumber(8),18,6,13,6));
 auto cf=renderMergedDocument(closed,{closed.revision});assert(cf);for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)for(unsigned c=0;c<4;c++){const auto k=(y*32+x)*4+c;const bool gap=y==6&&x>=13&&x<=18;assert(cf.value().merged.bytes[k]==(gap?(c==3?255:0):leakedFrame.value().merged.bytes[k]));}
 ColorValue green=red;green.channels={0,1,0,0};auto inside=derivedHollowFillCommand(closed,source,overlay,upper,Id::fromNumber(8),8,8,green);assert(inside&&inside.value().operations.size()==1&&std::get<Paint>(inside.value().operations.front()).event.layerId==lower);assert(doc.apply(inside.value(),closed.revision));const auto filled=doc.snapshot();auto frame=renderMergedDocument(filled,{filled.revision});assert(frame);assert(frame.value().merged.bytes[(8*32+8)*4+1]==255&&frame.value().merged.bytes[(1*32+1)*4+1]==0);assert(!derivedHollowFillCommand(filled,source,overlay,upper,Id::fromNumber(9),15,6,green));
 auto hollow=hollowInkOverlayCommand(filled,source,overlay,Id::fromNumber(9),block,1);assert(hollow&&hollow.value().operations.size()==1&&doc.apply(hollow.value(),filled.revision));auto hollowed=doc.snapshot();ColorValue blue=red;blue.channels={0,0,1,0};auto interior=derivedHollowFillCommand(hollowed,source,overlay,upper,Id::fromNumber(10),12,12,blue);assert(interior&&doc.apply(interior.value(),hollowed.revision));const auto mixed=doc.snapshot();assert(!closeLineartGapCommand(mixed,source,overlay,Id::fromNumber(11),10,12,22,12));
 for(const auto&[id,res]:*original.resources)assert(*mixed.resources->at(id).bytes==*res.bytes&&sha256(*mixed.resources->at(id).bytes)==id);
 assert(doc.undo()&&doc.snapshot().sameContent(hollowed));assert(doc.undo()&&doc.snapshot().sameContent(filled));assert(doc.undo()&&doc.snapshot().sameContent(closed));assert(doc.undo()&&doc.snapshot().sameContent(leaked));assert(doc.redo()&&doc.redo()&&doc.redo()&&doc.redo()&&doc.snapshot().sameContent(mixed));
 ProjectStore store;const auto path=std::filesystem::temp_directory_path()/("close-gap-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");assert(store.save(path,mixed));auto recovered=store.recover(path);assert(recovered&&recovered.value().snapshot.sameContent(mixed));assert(derivedHollowFillCommand(recovered.value().snapshot,source,overlay,upper,Id::fromNumber(11),8,8,red));std::filesystem::remove(path);
 assert(!closeLineartGapCommand(original,source,overlay,Id::fromNumber(12),14,6,18,6)); // white anchor
 assert(!closeLineartGapCommand(original,source,overlay,Id::fromNumber(12),6,6,13,6)); // no white crossing
 assert(!closeLineartGapCommand(original,source,overlay,Id::fromNumber(12),32,6,18,6));
 assert(lineartGapPixels({0,0},{63,0},128,128));assert(!lineartGapPixels({0,0},{UINT32_MAX-1.,0},UINT32_MAX,1));assert(!lineartGapPixels({0,0},{64,0},128,128));assert(!lineartGapPixels({.5,0},{4,0},32,32));
 std::stop_source stop;stop.request_stop();assert(!closeLineartGapCommand(original,source,overlay,Id::fromNumber(12),13,6,18,6,stop.get_token()));
 auto malformed=closed;auto events=std::make_shared<EventList>(*closed.eventIndex);malformed.eventIndex=events;events->back().color.alpha=.5;assert(!hollowDerivedReference(malformed,source,overlay));*events=*closed.eventIndex;events->back().geometry.front().y=events->back().geometry.back().y=7;assert(!hollowDerivedReference(malformed,source,overlay));
 // Black arbitrary area paint cannot masquerade as a short straight barrier.
 *events=*closed.eventIndex;auto& e=events->back();e.geometry.push_back({13,7,1,{},{},e.startUs,0,0});e.geometry.push_back({19,7,1,{},{},e.startUs,0,0});assert(!hollowDerivedReference(malformed,source,overlay));
 std::cout<<"PASS controlled gap: canonical visible short barrier, closure-only lower partition, historical paint unchanged, later hollow+upper, undo/reopen, malformed/anchor/overlap/cancel rejection\n";
}
