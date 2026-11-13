#include "brush/CubicInkStroke.h"
#include "brush/FreehandStroke.h"
#include "brush/BucketFill.h"
#include "core/EditableCubic.h"
#include "render/ReferenceRenderer.h"
#include "storage/ProjectStore.h"
#include "jobs/Protocol.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
struct Host final:WorkerHost {Result<void> start(const JobRequest&)override{return {};}void requestCancel(JobId)override{}void terminate(JobId)override{}};
struct TestClock final:Clock{std::uint64_t millis()const override{return 0;}};
Result<ChangeSet> external(Document&doc,const DocumentSnapshot&next){Host host;TestClock clock;ResourceGovernor governor;JobBroker broker(host,governor,clock);auto old=doc.snapshot();JobRequest q;q.id=Id::fromNumber(950);q.nonce=Id::fromNumber(951);q.task="native.test";q.resources.id=q.id;q.inputSnapshot={old.documentId,old.revision,stateHash(old).value()};q.snapshotBytes=encodeDelta(DocumentSnapshot{},old).value();assert(broker.submit(q));JobResult r;r.id=q.id;r.nonce=q.nonce;r.inputRevision=old.revision;r.delta=encodeDelta(old,next).value();r.outputHash=sha256(r.delta);return broker.accept(r,doc);}
int main(){
 NewDocumentRequest req;req.dimensions={1024,1024,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto made=Document::create(spec.value(),Id::fromNumber(90));assert(made);auto&doc=made.value();const auto layer=Id::fromNumber(2),later=Id::fromNumber(3),curveId=Id::fromNumber(10);
 for(auto id:{layer,later}){auto add=addFillLayerCommand(doc.snapshot(),id);assert(add&&doc.apply(add.value(),doc.snapshot().revision));}
 auto blank=doc.snapshot();std::array<std::array<double,2>,4> controls{{{20,20},{20,1000},{1000,1000},{1000,20}}};ColorValue black;
 auto create=cubicInkStrokeCommand(blank,layer,curveId,controls,4,black);assert(create&&doc.apply(create.value(),blank.revision));auto original=doc.snapshot();assert(original.eventIndex->back().geometry.size()==4&&original.eventIndex->back().engineVersion==23);
 ReferenceRenderer renderer;auto p0=renderer.render(original,{layer,0,0,0},{original.revision});auto p1=renderer.render(original,{layer,1,0,0},{original.revision});assert(p0&&p1&&renderer.statistics().cubicPreparations==1&&renderer.statistics().cubicExpandedPoints>4);
 auto again=renderer.render(original,{layer,0,0,0},{original.revision});assert(again&&again.value().bytes==p0.value().bytes&&renderer.statistics().cubicPreparations==1);
 auto altered=controls;altered[1][0]=500;auto edit=adjustCubicControlsCommand(original,curveId,altered);assert(edit&&doc.apply(edit.value(),original.revision));auto edited=doc.snapshot();assert(edited.eventIndex->back().geometry[1].x==500&&original.eventIndex->back().geometry[1].x==20);
 auto changed=renderer.render(edited,{layer,0,0,0},{edited.revision});assert(changed&&changed.value().bytes!=p0.value().bytes&&renderer.statistics().cubicPreparations==2);
 assert(doc.undo()&&doc.snapshot().sameContent(original));assert(doc.redo()&&doc.snapshot().sameContent(edited));
 std::vector<InputSample> points{{5,5,1,{},{},0,0,0},{40,20,1,{},{},10,0,0}};auto hand=smoothInkStrokeCommand(doc.snapshot(),later,Id::fromNumber(20),points,2,black);assert(hand&&doc.apply(hand.value(),doc.snapshot().revision));const auto laterExact=doc.snapshot().eventIndex->back();auto adjustAgain=adjustCubicControlsCommand(doc.snapshot(),curveId,controls);assert(adjustAgain&&doc.apply(adjustAgain.value(),doc.snapshot().revision));assert(doc.snapshot().eventIndex->back()==laterExact);
 auto noBarrier=doc.snapshot();StrokeEvent bucket;bucket.id=Id::fromNumber(30);bucket.layerId=later;bucket.objectId=noBarrier.nodes->at(later).objectId;bucket.parentRevision=noBarrier.revision;bucket.engineVersion=8;bucket.toolId="bucket";bucket.geometry={{20,20,1,{},{},0,0,0},{30,20,1,{},{},0,0,0}};assert(doc.apply(Command(Paint{bucket,{0,0,10,1}}),noBarrier.revision));auto barrier=doc.snapshot();assert(!adjustCubicControlsCommand(barrier,curveId,altered));assert(!doc.apply(Command(AdjustCubicControls{curveId,altered}),barrier.revision)&&doc.snapshot().sameContent(barrier));assert(doc.undo());
 auto current=doc.snapshot();auto rewritten=std::make_shared<EventList>(*current.eventIndex);rewritten->front().geometry[1].x=400;ConvertColor conversion;conversion.target=current.spec.colorSpec;conversion.events=rewritten;conversion.paperColor=current.spec.background.paperColor;assert(!doc.apply(Command(conversion),current.revision)&&doc.snapshot().sameContent(current));
 Command batch(AdjustCubicControls{curveId,altered});batch.operations.push_back(RenameNode{layer,"changed"});assert(!doc.apply(batch,current.revision)&&doc.snapshot().sameContent(current));
 auto huge=controls;huge[1][0]=1e9;assert(!adjustCubicControlsCommand(current,curveId,huge));assert(!doc.apply(Command(AdjustCubicControls{curveId,huge}),current.revision));
 auto fresh=Document::fromSnapshot(blank);assert(fresh);auto candidate=blank;auto events=std::make_shared<EventList>(*original.eventIndex);candidate.eventIndex=events;candidate.revision=blank.revision+1;auto rejected=external(fresh.value(),candidate);assert(!rejected&&rejected.error().messageKey=="worker.cubic-not-supported"&&fresh.value().snapshot().sameContent(blank));
 SetProperties lock;lock.id=layer;lock.locked=true;assert(doc.apply(Command(lock),current.revision));auto locked=doc.snapshot();assert(!adjustCubicControlsCommand(locked,curveId,altered));assert(!doc.apply(Command(AdjustCubicControls{curveId,altered}),locked.revision));
 std::stop_source stop;stop.request_stop();assert(!cubicInkStrokeCommand(blank,layer,Id::fromNumber(99),controls,4,black,1,stop.get_token()));ReferenceRenderer cancelled;assert(!cancelled.prepare(original,stop.get_token())&&cancelled.statistics().cubicPreparations==0);
 std::cout<<"PASS cubic core authoring: four controls, cached once across tiles, actual changed render, Undo, later native preserved, unknown dependency/multiaction/color-conversion/worker/locked/bounds guards; UI not yet implemented\n";
}
