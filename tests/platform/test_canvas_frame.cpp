#include "platform/CanvasFrame.h"
#include "core/LineartRaster.h"
#include "core/MyPaintCroppedRaster.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest r;r.dimensions={4,4,Unit::Px,300};r.transparent=false;r.paperColor.channels={1,1,1,0};r.paperColor.alpha=1;
 auto spec=validateDocument(r,{});assert(spec);auto doc=Document::create(spec.value(),Id::fromNumber(1));assert(doc);
 Bytes rgba(4*4*4,0);rgba[3]=255;Bytes png={137,80,78,71,13,10,26,10};
 auto ink=makeLineartRasterCommand(rgba,png,4,4,Id::fromNumber(2));assert(ink);assert(doc.value().apply(ink.value(),0));
 auto s=doc.value().snapshot();auto frame=renderCanvasFrame(s,UINT64_MAX);assert(frame);assert(frame.value().pixelColor(0,0)==QColor(0,0,0));assert(frame.value().pixelColor(3,3)==QColor(255,255,255));
 auto stroke=croppedRaster::makeNormal({255,0,0,128},png,{'n'},{2,2,1,1},4,4,Id::fromNumber(3));assert(stroke);assert(doc.value().apply(stroke.value(),s.revision));s=doc.value().snapshot();
 auto rendered=renderCanvasFrame(s,UINT64_MAX);assert(rendered);auto expected=renderMergedDocument(s,{s.revision});assert(expected);
 for(int y=0;y<4;y++)for(int x=0;x<4;x++)for(int c=0;c<4;c++)assert(rendered.value().constScanLine(y)[x*4+c]==expected.value().merged.bytes[(y*4+x)*4+c]);
 assert(rendered.value().pixelColor(2,2)==QColor(255,127,127));
 // QImage owns its pixels, even after the temporary render result is destroyed.
 auto retained=rendered.value();rendered=error(ErrorCode::Cancelled,"test");assert(retained.pixelColor(2,2)==QColor(255,127,127));
 MergedBudget tooSmall;tooSmall.maxBytes=1;assert(!renderCanvasFrame(s,UINT64_MAX,tooSmall));
 auto transformed=s;auto nodes=std::make_shared<NodeMap>(*s.nodes);nodes->at(Id::fromNumber(2)).transform.affine[4]=1;transformed.nodes=nodes;assert(!renderCanvasFrame(transformed,UINT64_MAX));

 // Imported pixels remain visible during native-event replay; native paint follows the limit.
 LayerNode n;n.id=Id::fromNumber(9);n.parent=Document::rootId();
 assert(doc.value().apply(Command(AddNode{n}),s.revision));s=doc.value().snapshot();
 StrokeEvent e;e.id=Id::fromNumber(10);e.layerId=n.id;e.engineVersion=8;e.toolId="bucket";e.parentRevision=s.revision;e.color.channels={0,0,1,0};
 e.geometry={{0,3,1,{},{},0,0,0},{4,3,1,{},{},0,0,0}};
 assert(doc.value().apply(Command(Paint{e,{0,3,4,1}}),s.revision));s=doc.value().snapshot();
 auto before=renderCanvasFrame(s,0),after=renderCanvasFrame(s,1);assert(before&&after);
 assert(before.value().pixelColor(0,0)==QColor(0,0,0));assert(before.value().pixelColor(2,2)==QColor(255,127,127));
 assert(before.value().pixelColor(1,3)==QColor(255,255,255));assert(after.value().pixelColor(1,3)==QColor(0,0,255));
 auto hidden=s;auto hiddenNodes=std::make_shared<NodeMap>(*s.nodes);hiddenNodes->at(n.id).visible=false;hidden.nodes=hiddenNodes;
 auto hiddenFrame=renderCanvasFrame(hidden,UINT64_MAX);assert(hiddenFrame&&hiddenFrame.value()==before.value());
 assert(s.sameContent(doc.value().snapshot()));
 // Cropped raster spans the hardware-display tile edge without dropping pixels.
 NewDocumentRequest large;large.dimensions={514,514,Unit::Px,300};auto ls=validateDocument(large,{});assert(ls);auto ld=Document::create(ls.value(),Id::fromNumber(20));assert(ld);
 Bytes cross(16,255);auto crossCommand=croppedRaster::makeNormal(cross,png,{'x'},{511,511,2,2},514,514,Id::fromNumber(21));assert(crossCommand);assert(ld.value().apply(crossCommand.value(),0));
 auto crossFrame=renderCanvasFrame(ld.value().snapshot(),UINT64_MAX);assert(crossFrame);assert(crossFrame.value().pixelColor(511,511)==QColor(255,255,255));assert(crossFrame.value().pixelColor(512,512)==QColor(255,255,255));
 std::cout<<"PASS desktop frame imported ink + cropped Normal alpha, shared compositor parity, ownership, budget and unsupported rejection\n";
}
