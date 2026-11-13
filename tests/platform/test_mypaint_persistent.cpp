#include "platform/MyPaintRasterCommand.h"
#include "core/PersistentRasterPaint.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace drawborad;
static MyPaintGesture gesture(unsigned k){MyPaintGesture g;g.color.channels={.18,.24,.35,0};g.color.alpha=.8;g.opacity=.75;g.radiusScale=.5;for(unsigned i=0;i<13;++i){InputSample p;p.x=40+k*9+i*8;p.y=40+k*7+4*std::sin(i*.4);p.pressure=.15+.6*std::sin(3.141592653589793*i/12);p.timestampUs=1000000+i*12000;g.samples.push_back(p);}return g;}
static TileData merged(const DocumentSnapshot&s){auto r=renderMergedDocument(s,{s.revision,UINT64_MAX,UINT64_MAX});assert(r);return r.value().merged;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);const QString root=QStringLiteral(DRAWBORAD_SOURCE_DIR);MyPaintRuntime runtime{"/usr/bin/python3",root+"/cloud/mypaint_worker.py",root+"/cloud/mypaint_data/pins.json"};assert(validateMyPaintRuntime(runtime));
 NewDocumentRequest req;req.dimensions={700,360,Unit::Px,300};req.transparent=true;auto spec=validateDocument(req,{});assert(spec);auto created=Document::create(spec.value(),Id::fromNumber(77));assert(created);auto doc=std::move(created.value());const auto blank=doc.snapshot();const auto layer=Id::fromNumber(80);auto initial=persistentRaster::manifest({});assert(initial);LayerNode node;node.id=layer;node.parent=Document::rootId();node.stage=persistentRaster::Stage;node.sourceKind=SourceKind::ImportedRaster;node.sourceResource=initial.value().id;node.bounds={0,0,700,360};node.name="Actual MyPaint same layer";Command setup;setup.operations={InstallResource{initial.value()},AddNode{node}};assert(doc.apply(setup,doc.snapshot().revision));
 for(unsigned k=0;k<33;++k){const auto before=doc.snapshot();auto candidate=buildMyPaintPersistentCommand(before,before.revision,gesture(k),layer,Id::fromNumber(100+k),runtime);if(!candidate){std::cerr<<"stroke "<<k<<' '<<candidate.error().messageKey<<'\n';return 1;}assert(doc.snapshot().sameContent(before));assert(doc.apply(candidate.value().command,candidate.value().baseRevision));assert(doc.snapshot().nodes->size()==blank.nodes->size()+1);auto h=persistentRaster::read(doc.snapshot(),layer);assert(h&&h.value().records.size()==k+1&&h.value().cursor==k+1);}
 const auto full=doc.snapshot();assert(full.eventIndex->empty());const auto pixels=merged(full);auto stream=NativeLayerStream::create(full,{full.revision,UINT64_MAX,UINT64_MAX});assert(stream&&stream.value().size()==1);auto item=stream.value().read(0);assert(item&&item.value().pixels==pixels);
 assert(doc.apply(Command{StepRasterHistory{layer,full.nodes->at(layer).sourceResource,-1}},full.revision));const auto withdrawn=doc.snapshot();assert(persistentRaster::read(withdrawn,layer).value().cursor==32&&merged(withdrawn)!=pixels);
 auto branch=buildMyPaintPersistentCommand(withdrawn,withdrawn.revision,gesture(0),layer,Id::fromNumber(900),runtime);assert(!branch&&branch.error().messageKey=="paint-history.branch-required"&&doc.snapshot().sameContent(withdrawn));
 QTemporaryDir temp;assert(temp.isValid());const auto file=(temp.path()+"/persistent.draw").toStdString();ProjectStore store;assert(store.save(file,withdrawn));auto recovered=store.recover(file);assert(recovered&&recovered.value().snapshot.sameContent(withdrawn));auto reopened=Document::fromSnapshot(recovered.value().snapshot);assert(reopened);auto current=reopened.value().snapshot();assert(reopened.value().apply(Command{StepRasterHistory{layer,current.nodes->at(layer).sourceResource,1}},current.revision));assert(persistentRaster::read(reopened.value().snapshot(),layer).value().cursor==33&&merged(reopened.value().snapshot())==pixels);
 assert(reopened.value().undo()&&reopened.value().snapshot().sameContent(withdrawn));assert(reopened.value().redo()&&merged(reopened.value().snapshot())==pixels);
 std::stop_source stop;stop.request_stop();current=reopened.value().snapshot();auto cancelled=buildMyPaintPersistentCommand(current,current.revision,gesture(0),layer,Id::fromNumber(901),runtime,stop.get_token());assert(!cancelled&&cancelled.error().code==ErrorCode::Cancelled&&reopened.value().snapshot().sameContent(current));
 std::uint64_t bytes=0;for(const auto&[id,r]:*full.resources){(void)id;bytes+=r.bytes->size();}std::cout<<"PASS 33 actual pinned MyPaint strokes one node; persistent withdraw/reopen/restore exact pixels, layer stream, Undo/Redo, branch refusal/cancel; current-resource bytes="<<bytes<<'\n';
}
