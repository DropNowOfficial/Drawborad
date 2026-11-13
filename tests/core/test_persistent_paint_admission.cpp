#include "core/PersistentRasterPaint.h"
#include "core/Commands.h"
#include "storage/ProjectStore.h"
#include "jobs/Protocol.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
const auto layer=Id::fromNumber(55);
Document fresh(){NewDocumentRequest req;req.dimensions={64,64,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto d=Document::create(spec.value(),Id::fromNumber(51));assert(d);auto m=persistentRaster::manifest({});assert(m);LayerNode n;n.id=layer;n.parent=Document::rootId();n.stage=persistentRaster::Stage;n.sourceKind=SourceKind::ImportedRaster;n.sourceResource=m.value().id;n.bounds={0,0,64,64};Command c;c.operations={InstallResource{m.value()},AddNode{n}};assert(d.value().apply(c,d.value().snapshot().revision));return std::move(d.value());}
struct Host final:WorkerHost{Result<void>start(const JobRequest&)override{return{};}void requestCancel(JobId)override{}void terminate(JobId)override{}};
struct TestClock final:Clock{std::uint64_t millis()const override{return 0;}};
Result<ChangeSet> external(Document&doc,const DocumentSnapshot&next){Host h;TestClock t;ResourceGovernor g;JobBroker broker(h,g,t);auto old=doc.snapshot();JobRequest q;q.id=Id::fromNumber(950);q.nonce=Id::fromNumber(951);q.task="test";q.resources.id=q.id;q.inputSnapshot={old.documentId,old.revision,stateHash(old).value()};q.snapshotBytes=encodeDelta({},old).value();assert(broker.submit(q));JobResult r;r.id=q.id;r.nonce=q.nonce;r.inputRevision=old.revision;r.delta=encodeDelta(old,next).value();r.outputHash=sha256(r.delta);return broker.accept(r,doc);}
int main(){
 {auto d=fresh();Resource r;r.kind=ResourceKind::Reference;r.bytes=std::make_shared<const Bytes>(8*1024*1024,7);r.id=sha256(*r.bytes);assert(d.apply(Command{InstallResource{r}},d.snapshot().revision));auto body=d.snapshot().resources->at(r.id).bytes;
 // Shared bytes retained by many snapshots count once, not once per snapshot.
 for(int i=0;i<100;++i){assert(d.apply(Command{RenameNode{layer,"rename"+std::to_string(i)}},d.snapshot().revision));assert(d.snapshot().resources->at(r.id).bytes==body);}
 unsigned copies=0;for(;copies<32;++copies){auto before=d.snapshot();auto result=d.apply(Command{InstallResource{r}},before.revision);if(!result){assert(result.error().code==ErrorCode::BudgetExceeded);assert(d.snapshot().sameContent(before));break;}assert(d.snapshot().resources->at(r.id).bytes!=body);body=d.snapshot().resources->at(r.id).bytes;}
 assert(copies>5&&copies<16);std::cout<<"distinct ownership samehash rejected after "<<copies<<" additional copies\n";}
 {auto d=fresh();Command add;for(unsigned i=0;i<256;++i){Resource r;r.kind=ResourceKind::Reference;Bytes b{std::uint8_t(i),4};r.id=sha256(b);r.bytes=std::make_shared<const Bytes>(std::move(b));add.operations.push_back(InstallResource{r});}assert(d.apply(add,d.snapshot().revision));unsigned count=0;
 for(;count<2000;++count){auto before=d.snapshot();auto result=d.apply(Command{RenameNode{layer,"metadata"+std::to_string(count)}},before.revision);if(!result){assert(result.error().code==ErrorCode::BudgetExceeded&&d.snapshot().sameContent(before));break;}}
 assert(count>100&&count<1500);std::cout<<"unique history map entry bound rejected after "<<count<<" renames\n";}
 {auto d=fresh();auto original=d.snapshot();auto patch=persistentRaster::makePatch(original,{2,2,2,2},Bytes(16,127));assert(patch);Command add;add.operations={InstallResource{patch.value()},AppendRasterPatch{layer,original.nodes->at(layer).sourceResource,{Id::fromNumber(100),patch.value().id,255}}};assert(d.apply(add,original.revision));auto active=d.snapshot();auto activeHash=active.nodes->at(layer).sourceResource;auto activeBody=active.resources->at(activeHash).bytes;
 assert(d.apply(Command{StepRasterHistory{layer,activeHash,-1}},active.revision));auto inactive=d.snapshot();auto inactiveHash=inactive.nodes->at(layer).sourceResource;auto inactiveBody=inactive.resources->at(inactiveHash).bytes;
 for(int i=0;i<50;++i){auto s=d.snapshot();assert(d.apply(Command{StepRasterHistory{layer,s.nodes->at(layer).sourceResource,1}},s.revision));assert(d.snapshot().resources->at(activeHash).bytes==activeBody);s=d.snapshot();assert(d.apply(Command{StepRasterHistory{layer,s.nodes->at(layer).sourceResource,-1}},s.revision));assert(d.snapshot().resources->at(inactiveHash).bytes==inactiveBody);}
 auto old=d.snapshot();auto changed=old;auto ns=std::make_shared<NodeMap>(*old.nodes);ns->at(layer).sourceResource=activeHash;changed.nodes=ns;changed.revision=old.revision+1;assert(validateSnapshot(changed));assert(!external(d,changed)&&d.snapshot().sameContent(old));
 auto removed=old;ns=std::make_shared<NodeMap>(*old.nodes);ns->erase(layer);ns->at(Document::rootId()).children.clear();removed.nodes=ns;removed.revision=old.revision+1;assert(validateSnapshot(removed));assert(!external(d,removed)&&d.snapshot().sameContent(old));
 auto noHistory=Document::create(old.spec,old.documentId);assert(noHistory);auto introduced=old;introduced.revision=noHistory.value().snapshot().revision+1;assert(!external(noHistory.value(),introduced));}
 std::cout<<"PASS retained unique-vs-distinct Bytes, map metadata bound, immutable manifest reuse and worker addition/removal/history rewrite refusal\n";
}
