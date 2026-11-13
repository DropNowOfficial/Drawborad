#include "brush/HollowInkCommand.h"
#include "storage/ProjectStore.h"
#include "ui/MergedDocument.h"
#include <cassert>
#include <chrono>
using namespace drawborad;
int main(){
 NewDocumentRequest req;req.transparent=false;req.paperColor.channels={1,1,1,0};req.paperColor.alpha=1;req.dimensions={64,64,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);auto made=Document::create(spec.value(),Id::fromNumber(1));assert(made);auto& doc=made.value();
 Bytes raw(64*64*4,255);std::vector<Rgba8> rgba(64*64,{255,255,255,255});
 for(unsigned y=10;y<40;y++)for(unsigned x=10;x<40;x++){auto k=(y*64+x)*4;raw[k]=raw[k+1]=raw[k+2]=0;rgba[y*64+x]={0,0,0,255};}
 // An independent fine mark remains completely untouched.
 for(unsigned x=45;x<60;x++){auto k=(20*64+x)*4;raw[k]=raw[k+1]=raw[k+2]=0;}
 auto source=Id::fromNumber(2),overlay=Id::fromNumber(3);auto imported=makeLineartRasterCommand(raw,{'s','y','n','t','h'},64,64,source);assert(imported&&doc.apply(imported.value(),0));const auto original=doc.snapshot();
 auto selected=floodFill(rgba,64,64,20,20,0);assert(selected&&selected.value().pixelCount==900);
 auto transparent=original;transparent.spec.background.transparent=true;assert(!hollowInkOverlayCommand(transparent,source,overlay,Id::fromNumber(4),selected.value(),1));
 std::stop_source cancelled;cancelled.request_stop();assert(!hollowInkOverlayCommand(original,source,overlay,Id::fromNumber(4),selected.value(),1,cancelled.get_token()));
 FillMask thin;thin.runs={{20,20,2}};thin.pixelCount=2;assert(!hollowInkOverlayCommand(original,source,overlay,Id::fromNumber(4),thin,1));
 auto faded=Document::fromSnapshot(original);assert(faded);SetProperties fade;fade.id=Document::rootId();fade.opacity=.5;assert(faded.value().apply(Command{fade},faded.value().snapshot().revision));assert(!hollowInkOverlayCommand(faded.value().snapshot(),source,overlay,Id::fromNumber(4),selected.value(),1));
 auto colored=Document::create(spec.value(),Id::fromNumber(20));assert(colored);auto coloredRaw=raw;coloredRaw[(20*64+20)*4]=30;auto coloredSource=makeLineartRasterCommand(coloredRaw,{'c'},64,64,source);assert(coloredSource&&colored.value().apply(coloredSource.value(),0));assert(!hollowInkOverlayCommand(colored.value().snapshot(),source,overlay,Id::fromNumber(4),selected.value(),1));
 auto command=hollowInkOverlayCommand(original,source,overlay,Id::fromNumber(4),selected.value(),1);assert(command&&doc.apply(command.value(),original.revision));const auto changed=doc.snapshot();
 auto before=renderMergedDocument(original,{original.revision}),after=renderMergedDocument(changed,{changed.revision});assert(before&&after);
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){const auto k=(y*64+x)*4;const bool interior=x>10&&x<39&&y>10&&y<39;for(unsigned c=0;c<4;c++)assert(after.value().merged.bytes[k+c]==(interior?255:before.value().merged.bytes[k+c]));}
 for(const auto&[id,res]:*original.resources){assert(changed.resources->at(id).bytes==res.bytes);assert(sha256(*res.bytes)==id);}
 assert(doc.undo());assert(doc.snapshot().sameContent(original));assert(doc.redo());assert(doc.snapshot().sameContent(changed));SetProperties hide;hide.id=overlay;hide.visible=false;assert(doc.apply(Command{hide},doc.snapshot().revision));auto hidden=renderMergedDocument(doc.snapshot(),{doc.snapshot().revision});assert(hidden&&hidden.value().merged.bytes==before.value().merged.bytes);assert(doc.undo());
 auto path=std::filesystem::temp_directory_path()/("drawborad-hollow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".draw");ProjectStore store;assert(store.save(path,changed));auto reopened=store.recover(path);assert(reopened&&reopened.value().snapshot.sameContent(changed));std::filesystem::remove(path);
}
