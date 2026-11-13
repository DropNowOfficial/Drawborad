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
 std::cout<<"PASS derived hollow fill: atomic new layer, source-only repeated reference, exact old resources/cover, undo/redo/save/reopen, stack/color/edge/cancel guards\n";
}
