#include "Test.h"
#include "RenderFixture.h"
#include "reconstruct/StyleRegistry.h"
#include "render/ReferenceRenderer.h"
#include "storage/ProjectStore.h"
#include "storage/ProjectCodec.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
using namespace drawborad;
namespace {
StyleRegistry registry(){return StyleRegistry(std::filesystem::path(DRAWBORAD_STYLE_ROOT));}
DocumentSnapshot base(){
    auto s=fixture::snapshot(ColorMode::RGB,Depth::U8,64,64);
    return fixture::withEvents(s,{fixture::stroke(s,10,14,50,42)});
}
PixelHash picture(const DocumentSnapshot& s){
    ReferenceRenderer r;
    auto v=r.renderRegion(s,Id::fromNumber(3),{0,0,64,64},
                          {s.revision,s.eventIndex->size(),UINT64_MAX});
    CHECK(v);CHECK(r.statistics().sourceAssetReads==0);
    return canonicalPixelHash(v.value());
}
}
TEST("E05 loads five distinct offline versioned media presets") {
    const std::vector<std::string> styles={"fidelity","dry-media","marker","pencil","concept-sketch"};
    std::set<std::string> signatures;
    for(const auto& id:styles){
        auto style=registry().load(id);CHECK(style);
        CHECK(style.value().schemaVersion==1);
        CHECK(style.value().id==id);
        CHECK(style.value().widthScale>0);
        CHECK(style.value().widthScale<=1);
        CHECK(style.value().opacityScale>0);
        CHECK(style.value().grainStrength>=0);
        signatures.insert(style.value().tool+std::to_string(style.value().widthScale)+
                         std::to_string(style.value().opacityScale));
    }
    CHECK(signatures.size()==5);
}
TEST("E05 fidelity leaves actual replay pixels and stroke media unchanged") {
    auto s=base();
    auto style=registry().load("fidelity");CHECK(style);
    auto variant=makeStyleVariant(s,style.value(),1234);CHECK(variant);
    CHECK(variant.value().style.fidelity);
    CHECK(picture(s)==picture(variant.value().snapshot));
    CHECK(variant.value().snapshot.eventIndex->at(0).style==s.eventIndex->at(0).style);
    CHECK(variant.value().snapshot.eventIndex->at(0).toolId==s.eventIndex->at(0).toolId);
}
TEST("E05 four manual-media variants produce distinct genuine brush renders") {
    auto s=base();
    std::set<std::string> hashes;
    for(auto id:{"dry-media","marker","pencil","concept-sketch"}){
        auto style=registry().load(id);CHECK(style);
        auto variant=makeStyleVariant(s,style.value(),900);CHECK(variant);
        const auto& event=variant.value().snapshot.eventIndex->at(0);
        CHECK(event.objectId==s.eventIndex->at(0).objectId);
        CHECK(event.layerId==s.eventIndex->at(0).layerId);
        CHECK(event.geometry.size()==s.eventIndex->at(0).geometry.size());
        CHECK(event.color==s.eventIndex->at(0).color);
        hashes.insert(picture(variant.value().snapshot).hex());
    }
    CHECK(hashes.size()==4);
    CHECK(!hashes.contains(picture(s).hex()));
}
TEST("E05 same seed produces identical variant; different seed changes manifest") {
    auto s=base(); auto style=registry().load("dry-media");CHECK(style);
    auto first=makeStyleVariant(s,style.value(),7);CHECK(first);
    auto again=makeStyleVariant(s,style.value(),7);CHECK(again);
    auto third=makeStyleVariant(s,style.value(),8);CHECK(third);
    CHECK(stateHash(first.value().snapshot).value()==stateHash(again.value().snapshot).value());
    CHECK(first.value().snapshot.documentId!=s.documentId);
    CHECK(third.value().snapshot.documentId!=first.value().snapshot.documentId);
    CHECK(first.value().provenanceResource!=third.value().provenanceResource);
}
TEST("E05 variant preserves original object IDs and original snapshot hash") {
    auto s=base();const auto original=stateHash(s).value();
    auto v=makeStyleVariant(s,registry().load("marker").value(),99);CHECK(v);
    CHECK(stateHash(s).value()==original);
    CHECK(*v.value().snapshot.nodes==*s.nodes);
    CHECK(v.value().snapshot.spec==s.spec);
    CHECK(v.value().snapshot.eventIndex->at(0).objectId==s.eventIndex->at(0).objectId);
    CHECK(v.value().snapshot.resources->contains(v.value().provenanceResource));
    CHECK(!s.resources->contains(v.value().provenanceResource));
    CHECK(validateSnapshot(v.value().snapshot));
}
TEST("E05 saved style variant reopens without model or original source") {
    auto s=base(); auto p=registry().load("concept-sketch");CHECK(p);
    auto v=makeStyleVariant(s,p.value(),2026);CHECK(v);
    auto dir=std::filesystem::temp_directory_path()/"drawborad-style-e05";
    std::filesystem::create_directories(dir);
    const auto path=dir/"variant.draw";
    ProjectStore store;
    CHECK(store.save(path,v.value().snapshot));
    auto reopened=store.recover(path);CHECK(reopened);
    CHECK(stateHash(reopened.value().snapshot).value()==stateHash(v.value().snapshot).value());
    CHECK(reopened.value().snapshot.resources->contains(v.value().provenanceResource));
    CHECK(picture(reopened.value().snapshot)==picture(v.value().snapshot));
    std::error_code ec;std::filesystem::remove(path,ec);
}
TEST("E05 refuses unknown style IDs and path traversal") {
    CHECK(!registry().load("does-not-exist"));
    CHECK(!registry().load("../fidelity"));
    CHECK(!registry().load("pencil/../../private"));
}
TEST("E05 bad or unexpected style metadata is rejected safely") {
    const auto dir=std::filesystem::temp_directory_path()/"drawborad-e05-bad-registry";
    std::filesystem::create_directories(dir);
    {std::ofstream out(dir/"pencil.json");out<<"{\"schemaVersion\":1,\"id\":\"pencil\",\"id\":\"evil\"}";}
    CHECK(!StyleRegistry(dir).load("pencil"));
    {std::ofstream out(dir/"pencil.json");out<<"{\"schemaVersion\":1,\"id\":\"pencil\",\"tool\":\"pencil\",\"widthScale\":999}";}
    CHECK(!StyleRegistry(dir).load("pencil"));
    std::error_code ec;std::filesystem::remove_all(dir,ec);
}
TEST("E05 CMYK16 variant keeps original profile and native pigment values") {
    auto snap=fixture::snapshot(ColorMode::CMYK,Depth::U16,64,64);
    auto s=fixture::withEvents(snap,{fixture::stroke(snap,10,14,50,42)});
    auto style=registry().load("dry-media");CHECK(style);
    auto v=makeStyleVariant(s,style.value(),2027);CHECK(v);
    CHECK(v.value().snapshot.spec.colorSpec==s.spec.colorSpec);
    CHECK(v.value().snapshot.eventIndex->at(0).color==s.eventIndex->at(0).color);
    CHECK(validateSnapshot(v.value().snapshot));
    CHECK(!picture(v.value().snapshot).empty());
}
TEST("E05 style variants never enlarge a previously safe thin stroke footprint") {
    auto s=base();
    auto sourceEvents=std::make_shared<EventList>(*s.eventIndex);
    (*sourceEvents)[0].style.width=0.1;
    s.eventIndex=sourceEvents;
    for(auto name:{"dry-media","marker","pencil","concept-sketch"}){
        auto preset=registry().load(name);CHECK(preset);
        auto v=makeStyleVariant(s,preset.value(),17);CHECK(v);
        CHECK(v.value().snapshot.eventIndex->at(0).style.width<=0.1);
        CHECK(v.value().snapshot.eventIndex->at(0).style.width>0);
    }
}
TEST("E05 fidelity preserves engine random seeds to avoid changing replay") {
    auto s=base();
    auto v=makeStyleVariant(s,registry().load("fidelity").value(),223);CHECK(v);
    CHECK(v.value().snapshot.eventIndex->at(0).seed==s.eventIndex->at(0).seed);
}
TEST("E05 forged style cannot turn pigment event into a bitmap or source handle") {
    auto s=base();
    auto style=registry().load("marker");CHECK(style);
    style.value().tool="bitmap";
    auto result=makeStyleVariant(s,style.value(),42);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::UnsupportedFeature);
}
TEST("E05 variant provenance resource cannot silently alias incompatible existing bytes") {
    auto s=base();
    auto style=registry().load("pencil");CHECK(style);
    auto first=makeStyleVariant(s,style.value(),71);CHECK(first);
    auto resources=std::make_shared<ResourceMap>(*s.resources);
    Resource forged;
    forged.id=first.value().provenanceResource;
    forged.kind=ResourceKind::Reference;
    forged.bytes=std::make_shared<const Bytes>(Bytes{17,19});
    resources->emplace(forged.id,std::move(forged));
    s.resources=resources;
    auto result=makeStyleVariant(s,style.value(),71);
    CHECK(!result);
    CHECK(result.error().code==ErrorCode::DigestMismatch);
}
