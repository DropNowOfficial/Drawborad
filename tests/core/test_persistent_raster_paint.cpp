#include "core/PersistentRasterPaint.h"
#include "core/Commands.h"
#include "ui/MergedDocument.h"
#include "ui/LayerStream.h"
#include "storage/ProjectStore.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest req;req.dimensions={700,520,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto created=Document::create(spec.value(),Id::fromNumber(81));assert(created);auto doc=std::move(created.value());const auto blank=doc.snapshot();
 auto initial=persistentRaster::manifest({});assert(initial);LayerNode n;n.id=Id::fromNumber(900);n.parent=Document::rootId();n.stage=persistentRaster::Stage;n.sourceKind=SourceKind::ImportedRaster;n.sourceResource=initial.value().id;n.bounds={0,0,700,520};n.name="One persistent paint layer";
 Command start;start.operations={InstallResource{initial.value()},AddNode{n}};assert(doc.apply(start,doc.snapshot().revision));
 Bytes rgba(20*20*4);for(std::size_t p=0;p<rgba.size();p+=4){rgba[p]=210;rgba[p+1]=50;rgba[p+2]=20;rgba[p+3]=127;}
 auto patch=persistentRaster::makePatch(doc.snapshot(),{505,250,20,20},rgba);assert(patch);assert(doc.apply(Command{InstallResource{patch.value()}},doc.snapshot().revision));
 for(unsigned k=0;k<40;++k){auto s=doc.snapshot();assert(doc.apply(Command{AppendRasterPatch{n.id,s.nodes->at(n.id).sourceResource,{Id::fromNumber(1000+k),patch.value().id,128}}},s.revision));}
 auto full=doc.snapshot();assert(full.nodes->size()==blank.nodes->size()+1);auto history=persistentRaster::read(full,n.id);assert(history&&history.value().records.size()==40&&history.value().cursor==40);
 auto prepared=persistentRaster::Prepared::prepare(full);assert(prepared&&prepared.value().uniquePatchCount()==1);Bytes region(40*40*4),left(7*40*4),right(33*40*4);assert(prepared.value().render(full,n.id,{505,240,40,40},region));assert(prepared.value().render(full,n.id,{505,240,7,40},left));assert(prepared.value().render(full,n.id,{512,240,33,40},right));for(unsigned y=0;y<40;++y){assert(std::equal(left.begin()+y*28,left.begin()+(y+1)*28,region.begin()+y*160));assert(std::equal(right.begin()+y*132,right.begin()+(y+1)*132,region.begin()+y*160+28));}
 assert(region[(10*40)*4+3]>200&&region[3]==0);auto merged=renderMergedDocument(full,{full.revision,UINT64_MAX,UINT64_MAX});assert(merged);auto stream=NativeLayerStream::create(full,{full.revision,UINT64_MAX,UINT64_MAX});assert(stream);auto layer=stream.value().read(0);assert(layer&&layer.value().pixels.bytes[(250*700+505)*4+3]>200);
 assert(doc.apply(Command{StepRasterHistory{n.id,full.nodes->at(n.id).sourceResource,-1}},full.revision));auto reduced=doc.snapshot();assert(persistentRaster::read(reduced,n.id).value().cursor==39);assert(!doc.apply(Command{AppendRasterPatch{n.id,reduced.nodes->at(n.id).sourceResource,{Id::fromNumber(5000),patch.value().id,255}}},reduced.revision));assert(doc.snapshot().sameContent(reduced));
 assert(doc.undo()&&doc.snapshot().sameContent(full));assert(doc.redo()&&doc.snapshot().sameContent(reduced));
 auto bytes=encodeDelta({},doc.snapshot());assert(bytes);auto loaded=decodeDelta(bytes.value(),{});assert(loaded);auto reopened=Document::fromSnapshot(loaded.value());assert(reopened);auto r=reopened.value().snapshot();assert(reopened.value().apply(Command{StepRasterHistory{n.id,r.nodes->at(n.id).sourceResource,1}},r.revision));assert(persistentRaster::read(reopened.value().snapshot(),n.id).value().cursor==40);
 std::stop_source stop;stop.request_stop();assert(!persistentRaster::Prepared::prepare(full,stop.get_token()));Bytes untouched(40*40*4,77);assert(!prepared.value().render(full,n.id,{505,240,40,40},untouched,stop.get_token()));assert(std::all_of(untouched.begin(),untouched.end(),[](auto b){return b==77;}));
 auto changedSpec=full;changedSpec.spec.pixelSize.width=699;Bytes wrong(4,22);assert(!prepared.value().render(changedSpec,n.id,{0,0,1,1},wrong));assert(wrong==Bytes(4,22));
 // Candidate peak rejected before copying 65MiB input into this history document.
 Resource large;large.kind=ResourceKind::Reference;large.bytes=std::make_shared<const Bytes>(65*1024*1024,3);large.id=sha256(*large.bytes);auto beforeBudget=doc.snapshot();assert(!doc.apply(Command{InstallResource{large}},beforeBudget.revision));assert(doc.snapshot().sameContent(beforeBudget));
 auto invalid=full;auto nodes=std::make_shared<NodeMap>(*full.nodes);nodes->at(n.id).bounds.width=1;invalid.nodes=nodes;assert(!validateSnapshot(invalid));
 // Equal pixels with different immutable provenance must have different identities.
 Resource proofA;proofA.kind=ResourceKind::Reference;proofA.bytes=std::make_shared<const Bytes>(Bytes{1});proofA.id=sha256(*proofA.bytes);Resource proofB;proofB.kind=ResourceKind::Reference;proofB.bytes=std::make_shared<const Bytes>(Bytes{2});proofB.id=sha256(*proofB.bytes);
 auto a=persistentRaster::makePatch(full,{505,250,20,20},rgba,{proofA.id});auto b=persistentRaster::makePatch(full,{505,250,20,20},rgba,{proofB.id});assert(a&&b&&a.value().id!=b.value().id);
 auto provenanceSource=full;auto provenanceResources=std::make_shared<ResourceMap>(*full.resources);for(const auto&res:{proofA,proofB,a.value(),b.value()})(*provenanceResources)[res.id]=res;provenanceSource.resources=provenanceResources;
 assert(persistentRaster::patch(provenanceSource,a.value().id)&&persistentRaster::patch(provenanceSource,b.value().id));provenanceResources->at(a.value().id).dependencies={proofB.id};assert(!persistentRaster::patch(provenanceSource,a.value().id));
 std::cout<<"PASS 40 persistent strokes one node, ROI indexed cross-tile pixels, portable merged/layer stream, cursor/branch refusal/session Undo/Redo/reopen restore and pre-cancel\n";
}
