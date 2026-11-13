#include "brush/FreehandStroke.h"
#include "brush/ChiselInkStroke.h"
#include "brush/ContinuousEraser.h"
#include "brush/ClosedInkFill.h"
#include "storage/ProjectStore.h"
#include "storage/Binary.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace drawborad;
namespace {
DocumentSnapshot appendCubic(const DocumentSnapshot& before,NodeId layer,Id id){
 auto after=before;auto events=std::make_shared<EventList>(*before.eventIndex);
 std::uint64_t tick=0;for(const auto&e:*events)tick=std::max(tick,e.endUs+1);
 StrokeEvent e;e.id=id;e.layerId=layer;e.objectId=before.nodes->at(layer).objectId;e.parentRevision=before.revision;
 e.engineVersion=23;e.toolId="cubic-ink-aa";e.startUs=e.endUs=tick;e.style.width=4.25;e.style.opacity=.65;e.color.profileId=before.spec.colorSpec.profileId;
 e.geometry={{20.125,20.25,1,{},{},tick,0,0},{20.5,100.75,1,{},{},tick,0,0},{100.25,100.5,1,{},{},tick,0,0},{100.75,20.125,1,{},{},tick,0,0}};
 events->push_back(e);after.eventIndex=events;++after.revision;assert(validateSnapshot(after));return after;
}
std::string format(const Bytes& bytes){binary::Reader reader{bytes};return reader.text(32);}
void roundtrip(const DocumentSnapshot& snapshot){auto encoded=encodeDelta(DocumentSnapshot{},snapshot);assert(encoded&&format(encoded.value())=="DRAW-DELTA-23");auto decoded=decodeDelta(encoded.value(),DocumentSnapshot{});assert(decoded&&decoded.value().sameContent(snapshot));assert(decoded.value().eventIndex->back().engineVersion==23&&decoded.value().eventIndex->back().geometry.size()==4);assert(decoded.value().eventIndex->back().geometry==snapshot.eventIndex->back().geometry);}
}
int main(){
 NewDocumentRequest req;req.dimensions={128,128,Unit::Px,300};auto spec=validateDocument(req,{});assert(spec);
 auto simpleDoc=Document::create(spec.value(),Id::fromNumber(1));assert(simpleDoc);auto simpleLayer=Id::fromNumber(2);auto simpleAdd=addFillLayerCommand(simpleDoc.value().snapshot(),simpleLayer);assert(simpleAdd&&simpleDoc.value().apply(simpleAdd.value(),0));
 auto simple=appendCubic(simpleDoc.value().snapshot(),simpleLayer,Id::fromNumber(3));assert(simple.fillOperations.empty());roundtrip(simple);
 auto simpleBytes=encodeDelta(DocumentSnapshot{},simple);assert(simpleBytes);auto missingCount=simpleBytes.value();missingCount.resize(missingCount.size()-8);assert(!decodeDelta(missingCount,DocumentSnapshot{}));
 auto wrongHeader=simpleBytes.value();const std::string marker="DRAW-DELTA-23";auto at=std::search(wrongHeader.begin(),wrongHeader.end(),marker.begin(),marker.end());assert(at!=wrongHeader.end());*(at+marker.size()-1)='2';auto mislabeled=decodeDelta(wrongHeader,DocumentSnapshot{});assert(!mislabeled&&mislabeled.error().messageKey=="project.format-feature-mismatch");
 // Genuine fill provenance plus historical21 and22 events, not invented records.
 auto made=Document::create(spec.value(),Id::fromNumber(10));assert(made);auto&doc=made.value();auto base=Id::fromNumber(11),source=Id::fromNumber(12),ink=Id::fromNumber(13),free=Id::fromNumber(14);
 auto baseAdd=addFillLayerCommand(doc.snapshot(),base,true,true);assert(baseAdd&&doc.apply(baseAdd.value(),doc.snapshot().revision));
 auto sourceAdd=makeLineartRasterCommand(Bytes(128*128*4,255),{'w'},128,128,source);assert(sourceAdd&&doc.apply(sourceAdd.value(),doc.snapshot().revision));
 auto inkAdd=makeFinishingLayerCommand(doc.snapshot(),ink);assert(inkAdd&&doc.apply(inkAdd.value(),doc.snapshot().revision));
 std::vector<InputSample> square{{20,20,1,{},{},0,0,0},{60,20,1,{},{},10,0,0},{60,60,1,{},{},20,0,0},{20,60,1,{},{},30,0,0},{20,20,1,{},{},40,0,0}};ColorValue black;
 auto outline=smoothInkStrokeCommand(doc.snapshot(),ink,Id::fromNumber(15),square,2,black);assert(outline&&doc.apply(outline.value(),doc.snapshot().revision));
 auto fill=closedInkFillCommand(doc.snapshot(),ink,{Id::fromNumber(16),Id::fromNumber(17),Id::fromNumber(18),Id::fromNumber(19)});assert(fill&&doc.apply(fill.value(),doc.snapshot().revision));
 auto freeAdd=makeFinishingLayerCommand(doc.snapshot(),free);assert(freeAdd&&doc.apply(freeAdd.value(),doc.snapshot().revision));
 std::vector<InputSample> line{{20,90,1,{},{},0,0,0},{100,90,1,{},{},100,0,0}};
 auto chisel=chiselInkStrokeCommand(doc.snapshot(),free,Id::fromNumber(20),line,12,black,2,.7);assert(chisel&&doc.apply(chisel.value(),doc.snapshot().revision));
 auto erased=continuousEraserCommand(doc.snapshot(),free,Id::fromNumber(21),line,2,.5);assert(erased&&doc.apply(erased.value(),doc.snapshot().revision));
 const auto legacy=doc.snapshot();assert(legacy.fillOperations.size()==1);auto legacyBytes=encodeDelta(DocumentSnapshot{},legacy);assert(legacyBytes&&format(legacyBytes.value())=="DRAW-DELTA-22");auto legacyOpen=decodeDelta(legacyBytes.value(),DocumentSnapshot{});assert(legacyOpen&&legacyOpen.value().sameContent(legacy));
 const auto mixed=appendCubic(legacy,free,Id::fromNumber(22));assert(mixed.fillOperations==legacy.fillOperations);roundtrip(mixed);
 auto mixedBytes=encodeDelta(DocumentSnapshot{},mixed);assert(mixedBytes);auto cut=mixedBytes.value();cut.pop_back();assert(!decodeDelta(cut,DocumentSnapshot{}));
 auto prefixDelta=encodeDelta(legacy,mixed);assert(prefixDelta);auto prefixed=decodeDelta(prefixDelta.value(),legacy);assert(prefixed&&prefixed.value().sameContent(mixed));
 // Same-format edited controls are preserved as four controls, not replay samples.
 auto edited=mixed;auto editedEvents=std::make_shared<EventList>(*mixed.eventIndex);editedEvents->back().geometry[1].x=40.625;editedEvents->back().parentRevision=mixed.revision;edited.eventIndex=editedEvents;++edited.revision;assert(validateSnapshot(edited));roundtrip(edited);
 auto delta=encodeDelta(mixed,edited);assert(delta);auto changed=decodeDelta(delta.value(),mixed);assert(changed&&changed.value().sameContent(edited)&&changed.value().fillOperations==mixed.fillOperations);
 const auto root=std::filesystem::temp_directory_path()/("cubic-storage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));assert(std::filesystem::create_directory(root));auto oldPath=root/"old22.draw",newPath=root/"new23.draw";
 ProjectStore store;NativeFileIO io;assert(store.save(oldPath,legacy));auto oldHash=io.hash(oldPath,MaxProjectBytes);assert(oldHash);
 auto refused=store.save(oldPath,mixed);assert(!refused&&refused.error().messageKey=="save.format-copy-required");assert(!store.appendJournal(oldPath,legacy,mixed));auto afterHash=io.hash(oldPath,MaxProjectBytes);assert(afterHash&&afterHash.value()==oldHash.value());assert(!std::filesystem::exists(oldPath.string()+".journal"));
 assert(store.save(newPath,mixed));auto reopened=store.recover(newPath);assert(reopened&&reopened.value().snapshot.sameContent(mixed));
 auto newHash=io.hash(newPath,MaxProjectBytes);assert(newHash);assert(!store.save(newPath,legacy));auto preserved=io.hash(newPath,MaxProjectBytes);assert(preserved&&preserved.value()==newHash.value());
 assert(store.appendJournal(newPath,mixed,edited));auto journalOpen=store.recover(newPath);assert(journalOpen&&journalOpen.value().snapshot.sameContent(edited));
 // Store preserves its existing mixed-generation journal rejection.
 auto forbidden=encodeDelta(legacy,mixed);assert(forbidden);auto framed=frameRecord(forbidden.value());assert(framed);assert(io.write(oldPath.string()+".journal",framed.value(),true));auto badJournal=store.recover(oldPath);assert(!badJournal&&badJournal.error().messageKey=="project.mixed-format-journal");assert(io.hash(oldPath,MaxProjectBytes).value()==oldHash.value());
 auto backupPath=root/"mixed-backup.draw";assert(store.save(backupPath,legacy));auto newFile=io.read(newPath,MaxProjectBytes);assert(newFile);assert(io.write(backupPath.string()+".bak",newFile.value(),true));auto mixedBackup=store.recover(backupPath);assert(!mixedBackup&&mixedBackup.error().messageKey=="project.mixed-format-backup");
 if(const char* proof=std::getenv("DRAWBORAD_CUBIC_STORAGE_PROOF"))assert(store.save(proof,mixed));
 std::filesystem::remove_all(root);
 std::cout<<"PASS format23 exact four controls, empty/full provenance counts,21/22 mixed history, truncation/header refusal, save-copy protection, same-format journal and mixed-generation rejection\n";
}
