#include "brush/DryInkStroke.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest request;request.dimensions={512,512,Unit::Px,300};
 auto spec=validateDocument(request,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(21));assert(doc);
 LayerNode layer;layer.id=Id::fromNumber(2);layer.parent=Document::rootId();assert(doc.value().apply(Command(AddNode{layer}),0));
 const auto original=doc.value().snapshot();
 std::vector<InputSample> points{{10,20,.2,{},{},100,0,0},{100,120,1,{},{},1100,0,0}};
 ColorValue black;auto command=dryInkStrokeCommand(original,layer.id,Id::fromNumber(7),points,.25,black);assert(command);
 const auto& paint=std::get<Paint>(command.value().operations.at(0));
 assert(paint.event.engineVersion==12 && paint.event.toolId=="dry-ink");
 assert(paint.event.seed!=0);
 auto repeat=dryInkStrokeCommand(original,layer.id,Id::fromNumber(7),points,.25,black);assert(repeat);
 assert(std::get<Paint>(repeat.value().operations[0]).event.seed==paint.event.seed);
 auto distinct=dryInkStrokeCommand(original,layer.id,Id::fromNumber(70),points,.25,black);assert(distinct);
 assert(std::get<Paint>(distinct.value().operations[0]).event.seed!=paint.event.seed);
 assert(paint.event.startUs==0 && paint.event.endUs==1000 && paint.event.geometry[0].pressure==.2);
 assert(paint.event.parentRevision==original.revision && paint.event.style.width==.25);
 assert(doc.value().apply(command.value(),original.revision));auto painted=doc.value().snapshot();
 assert(doc.value().undo() && doc.value().snapshot().sameContent(original));assert(doc.value().redo() && doc.value().snapshot().sameContent(painted));
 auto next=dryInkStrokeCommand(painted,layer.id,Id::fromNumber(8),points,1,black);assert(next);
 assert(std::get<Paint>(next.value().operations.at(0)).event.startUs==1001);
 assert(!dryInkStrokeCommand(painted,layer.id,Id::fromNumber(7),points,1,black));
 assert(!dryInkStrokeCommand(original,layer.id,Id{},points,1,black));
 for(double width:{0.,.01,128.01,double(NAN)})assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),points,width,black));
 auto invalid=points;invalid[0].x=-1;assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),invalid,1,black));
 invalid=points;invalid[1].pressure=1.1;assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),invalid,1,black));
 invalid=points;invalid[1].timestampUs=1;assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),invalid,1,black));
 std::vector<InputSample> tooMany(4097,points.front());assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),tooMany,1,black));
 assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),{},1,black));
 black.alpha=2;assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),points,1,black));black.alpha=1;
 auto altered=original;auto nodes=std::make_shared<NodeMap>(*original.nodes);altered.nodes=nodes;
 nodes->at(layer.id).locked=true;auto denied=dryInkStrokeCommand(altered,layer.id,Id::fromNumber(9),points,1,black);assert(!denied && denied.error().code==ErrorCode::Locked);
 nodes->at(layer.id).locked=false;nodes->at(Document::rootId()).locked=true;
 denied=dryInkStrokeCommand(altered,layer.id,Id::fromNumber(9),points,1,black);assert(!denied && denied.error().code==ErrorCode::Locked);
 nodes->at(Document::rootId()).locked=false;nodes->at(layer.id).visible=false;assert(!dryInkStrokeCommand(altered,layer.id,Id::fromNumber(9),points,1,black));
 nodes->at(layer.id).visible=true;nodes->at(layer.id).transform.affine[4]=1;assert(!dryInkStrokeCommand(altered,layer.id,Id::fromNumber(9),points,1,black));
 nodes->at(layer.id).transform={};nodes->at(layer.id).sourceKind=SourceKind::ImportedRaster;assert(!dryInkStrokeCommand(altered,layer.id,Id::fromNumber(9),points,1,black));
 auto broken=original;broken.nodes.reset();assert(!dryInkStrokeCommand(broken,layer.id,Id::fromNumber(9),points,1,black));
 std::stop_source stop;stop.request_stop();assert(!dryInkStrokeCommand(original,layer.id,Id::fromNumber(9),points,1,black,1,stop.get_token()));
 auto overflow=painted;auto events=std::make_shared<EventList>(*painted.eventIndex);events->front().endUs=UINT64_MAX;events->front().geometry.back().timestampUs=UINT64_MAX;overflow.eventIndex=events;
 auto over=dryInkStrokeCommand(overflow,layer.id,Id::fromNumber(9),points,1,black);assert(!over && over.error().code==ErrorCode::Overflow);
 auto badCommand=command.value();std::get<Paint>(badCommand.operations.front()).event.style.width=129;
 auto fresh=Document::fromSnapshot(original);assert(fresh);assert(!fresh.value().apply(badCommand,original.revision));assert(fresh.value().snapshot().sameContent(original));
 assert(doc.value().snapshot().sameContent(painted));
 std::cout<<"dry ink command tests passed\n";
}
