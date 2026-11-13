#include "core/LineartRaster.h"
#include "storage/ProjectCodec.h"
#include <cassert>
#include <iostream>
using namespace drawborad;
int main(){
 NewDocumentRequest req;req.dimensions={8,8,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);
 auto doc=Document::create(spec.value(),Id::fromNumber(27));assert(doc);auto before=doc.value().snapshot();
 const Bytes manifest{'{','"','v','"',':','1','}'};Resource replay;replay.kind=ResourceKind::Reference;replay.id=sha256(manifest);replay.bytes=std::make_shared<const Bytes>(manifest);
 Bytes rgba(8*8*4,0);rgba[3]=128;Bytes png{137,80,78,71};
 auto result=makeLineartRasterCommand(rgba,png,8,8,Id::fromNumber(35));assert(result);auto command=result.value();
 auto& pngResource=std::get<InstallResource>(command.operations[0]).resource;pngResource.dependencies={replay.id};
 const auto pngId=pngResource.id;const auto rawId=std::get<InstallResource>(command.operations[1]).resource.id;
 auto& node=std::get<AddNode>(command.operations[2]).node;node.name="MyPaint ink (experimental raster)";
 command.operations.insert(command.operations.begin(),InstallResource{replay});
 assert(doc.value().apply(command,before.revision));auto after=doc.value().snapshot();
 assert(after.resources->at(rawId).dependencies==std::vector<Hash>{pngId});
 assert(after.resources->at(pngId).dependencies==std::vector<Hash>{replay.id});
 assert(after.nodes->at(Id::fromNumber(35)).locked && after.nodes->at(Id::fromNumber(35)).blend==BlendMode::Multiply);
 auto bytes=encodeDelta({},after);assert(bytes);auto restored=decodeDelta(bytes.value(),{});assert(restored && restored.value().sameContent(after));
 assert(*restored.value().resources->at(replay.id).bytes==manifest);
 assert(doc.value().undo() && doc.value().snapshot().sameContent(before));assert(doc.value().redo() && doc.value().snapshot().sameContent(after));
 // Same PNG with a different replay manifest must fail, not rewrite metadata used by old layers.
 auto duplicate=command;std::get<AddNode>(duplicate.operations[3]).node.id=Id::fromNumber(36);
 std::get<InstallResource>(duplicate.operations[1]).resource.dependencies.clear();
 assert(!doc.value().apply(duplicate,after.revision));assert(doc.value().snapshot().sameContent(after));
 std::cout<<"brush layer metadata, codec roundtrip, one undo/redo, immutable dependency collision passed\n";
}
