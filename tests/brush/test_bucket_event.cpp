#include "core/Commands.h"
#include "render/CoverageRasterizer.h"
#include "brush/BucketFill.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
void realFixture(const char* path) {
    auto recovered=ProjectStore{}.recover(path);assert(recovered);
    auto doc=Document::fromSnapshot(recovered.value().snapshot);assert(doc);
    auto before=doc.value().snapshot();
    assert(before.eventIndex->size()==4418); // Actual failed browser-acceptance artifact.
    auto layer=Id::fromNumber(0xfeed001),event=Id::fromNumber(0xfeed002);
    auto add=addFillLayerCommand(before,layer,true);assert(add);
    assert(doc.value().apply(add.value(),before.revision));
    auto added=doc.value().snapshot();
    auto reference=renderMergedDocument(added,{added.revision});assert(reference);
    ColorValue color;color.profileId=added.spec.colorSpec.profileId;
    color.channels={1,0,1,0};
    auto fill=bucketFillCommand(added,layer,event,reference.value().merged,0,0,255,color);assert(fill);
    auto unchanged=added;
    assert(doc.value().apply(fill.value(),added.revision));
    auto filled=doc.value().snapshot();assert(filled.eventIndex->size()==4419);
    assert(planMergedDocument(filled));
    auto output=renderMergedDocument(filled,{filled.revision});assert(output);
    for(std::size_t i=0;i<output.value().merged.bytes.size();i+=4){
        assert(output.value().merged.bytes[i]==255 && output.value().merged.bytes[i+1]==0 &&
               output.value().merged.bytes[i+2]==255 && output.value().merged.bytes[i+3]==255);
    }
    assert(doc.value().undo() && doc.value().snapshot().sameContent(unchanged));
    std::cout<<"real 4418-event browser fixture open/add/bucket/render/undo passed\n";
}
int main(int argc,char** argv) {
    if(argc==2)realFixture(argv[1]);
    NewDocumentRequest req; req.dimensions={8,8,Unit::Px,300};
    auto spec=validateDocument(req,{});
    assert(spec);
    auto doc=Document::create(spec.value(),Id::fromNumber(9));
    assert(doc);
    LayerNode layer;layer.id=Id::fromNumber(2);layer.parent=Document::rootId();
    assert(doc.value().apply(Command(AddNode{layer}),0));
    auto before=doc.value().snapshot();
    TileData reference;reference.width=8;reference.height=8;reference.rowStride=32;
    reference.bytes.resize(256,255);
    auto command=bucketFillCommand(before,layer.id,Id::fromNumber(99),reference,0,0,0,{});
    assert(command);
    auto built=Document::fromSnapshot(before);assert(built);
    assert(built.value().apply(command.value(),before.revision));
    auto builtState=built.value().snapshot();
    ReferenceRenderer renderer;
    auto tile=renderer.renderRegion(builtState,layer.id,{0,0,8,8},{builtState.revision});
    assert(tile);
    for (unsigned i=0;i<64;++i) assert(tile.value().bytes[4*i+3]==255);
    assert(built.value().undo() && built.value().snapshot().sameContent(before));
    assert(built.value().redo() && built.value().snapshot().sameContent(builtState));
    // Normal CI regression for documents beyond the former 4096-event ceiling.
    auto dense=before;auto denseEvents=std::make_shared<EventList>();
    for(unsigned i=0;i<4418;i++){
        StrokeEvent stroke;stroke.id=Id::fromNumber(10000+i);stroke.layerId=layer.id;
        stroke.engineVersion=2;stroke.toolId="fineliner";stroke.geometry={{1,1,1,{},{},0,0,0}};
        denseEvents->push_back(stroke);
    }
    dense.eventIndex=denseEvents;
    auto denseFill=bucketFillCommand(dense,layer.id,Id::fromNumber(90000),reference,0,0,255,{});
    assert(denseFill);
    auto denseDoc=Document::fromSnapshot(dense);assert(denseDoc);
    assert(denseDoc.value().apply(denseFill.value(),dense.revision));
    assert(denseDoc.value().snapshot().eventIndex->size()==4419);
    for(unsigned i=4418;i<8192;i++){
        auto stroke=denseEvents->front();stroke.id=Id::fromNumber(10000+i);denseEvents->push_back(stroke);
    }
    auto overLimit=bucketFillCommand(dense,layer.id,Id::fromNumber(90000),reference,0,0,255,{});
    assert(!overLimit && overLimit.error().messageKey=="bucket.document-budget");
    auto locked=before;auto nodes=std::make_shared<NodeMap>(*before.nodes);
    nodes->at(layer.id).locked=true;locked.nodes=nodes;
    assert(bucketFillCommand(locked,layer.id,Id::fromNumber(99),reference,0,0,0,{}).error().code==ErrorCode::Locked);
    nodes->at(layer.id).locked=false;nodes->at(layer.id).visible=false;
    assert(!bucketFillCommand(locked,layer.id,Id::fromNumber(99),reference,0,0,0,{}));
    nodes->at(layer.id).visible=true;nodes->at(layer.id).transform.affine[4]=1;
    assert(!bucketFillCommand(locked,layer.id,Id::fromNumber(99),reference,0,0,0,{}));
    StrokeEvent e;e.id=Id::fromNumber(3);e.layerId=layer.id;e.engineVersion=8;
    e.toolId="bucket";e.parentRevision=1;e.color.channels={1,0,0,0};
    e.geometry={{1,2,1,{}, {},0,0,0},{4,2,1,{}, {},0,0,0}};
    assert(doc.value().apply(Command(Paint{e,{1,2,3,1}}),1));
    auto filled=doc.value().snapshot();
    auto cov=strokeCoverage(e,{0,0,8,8},0);
    assert(cov);
    for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x)
        assert(cov.value()[y*8+x]==((y==2 && x>=1 && x<4)?65535:0));
    assert(doc.value().undo());
    assert(doc.value().snapshot().sameContent(before));
    assert(doc.value().redo());
    assert(doc.value().snapshot().sameContent(filled));
    auto bad=e;bad.geometry[1].y=3;assert(!validateRasterEvent(bad));
    bad=e;bad.geometry[0].x=.5;assert(!validateRasterEvent(bad));
    bad=e;bad.toolId="fineliner";assert(!validateRasterEvent(bad));
    bad=e;bad.geometry.push_back(e.geometry[0]);assert(!validateRasterEvent(bad));
    bad=e;bad.geometry.insert(bad.geometry.end(),e.geometry.begin(),e.geometry.end());
    assert(!validateRasterEvent(bad));
    bad=e;bad.geometry[1].x=9;
    auto next=Document::fromSnapshot(before);assert(next);
    assert(!next.value().apply(Command(Paint{bad,{}}),before.revision));
    assert(next.value().snapshot().sameContent(before));
    NewDocumentRequest largeReq;largeReq.dimensions={2048,2048,Unit::Px,300};
    auto largeSpec=validateDocument(largeReq,{});assert(largeSpec);
    auto large=Document::create(largeSpec.value(),Id::fromNumber(100));assert(large);
    for (unsigned i=0;i<2;++i) {
        auto add=addFillLayerCommand(large.value().snapshot(),Id::fromNumber(101+i));
        assert(add && large.value().apply(add.value(),large.value().snapshot().revision));
    }
    auto tooMany=addFillLayerCommand(large.value().snapshot(),Id::fromNumber(103));
    assert(!tooMany && tooMany.error().code==ErrorCode::BudgetExceeded);
    assert(addFillLayerCommand(large.value().snapshot(),Id::fromNumber(103),true));
    NewDocumentRequest printReq;printReq.dimensions={1725,2625,Unit::Px,300};
    auto printSpec=validateDocument(printReq,{});assert(printSpec);
    auto printDoc=Document::create(printSpec.value(),Id::fromNumber(200));assert(printDoc);
    assert(!addFillLayerCommand(printDoc.value().snapshot(),Id::fromNumber(201)));
    assert(addFillLayerCommand(printDoc.value().snapshot(),Id::fromNumber(201),true));
    std::cout<<"bucket event tests passed\n";
}
