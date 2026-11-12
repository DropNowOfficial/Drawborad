#include "storage/ProjectStore.h"
#include "storage/Binary.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
using namespace drawborad;
namespace fs=std::filesystem;
DocumentSnapshot sample() {
    NewDocumentRequest req;req.dimensions={16,16,Unit::Px,300};
    auto spec=validateDocument(req,{});
    assert(spec);
    auto doc=Document::create(spec.value(),Id::fromNumber(42));assert(doc);
    auto s=doc.value().snapshot();
    StrokeEvent e;e.id=Id::fromNumber(40);e.engineVersion=2;e.toolId="fineliner";
    e.geometry.push_back({1,1,1,{},{},0,0,0});
    auto events=std::make_shared<EventList>();events->push_back(e);s.eventIndex=events;
    return s;
}
void write(const fs::path& p,const Bytes& bytes) {
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    assert(f.good());
}
DocumentSnapshot bucketSample() {
    auto s=sample();
    auto events=std::make_shared<EventList>(*s.eventIndex);
    auto& e=events->front();e.engineVersion=8;e.toolId="bucket";
    e.geometry={{1,1,1,{},{},0,0,0},{8,1,1,{},{},0,0,0}};
    s.eventIndex=events;return s;
}
DocumentSnapshot lineartSample(bool bucket=true) {
    auto s=bucket?bucketSample():sample();
    auto raw=std::make_shared<Bytes>(16*16*4);for(std::size_t i=0;i<raw->size();++i)(*raw)[i]=std::uint8_t(i%251);
    Resource resource;resource.kind=ResourceKind::Tile;resource.bytes=raw;resource.id=sha256(*raw);
    auto resources=std::make_shared<ResourceMap>(*s.resources);(*resources)[resource.id]=resource;s.resources=resources;
    LayerNode layer;layer.id=Id::fromNumber(71);layer.parent=Document::rootId();layer.sourceKind=SourceKind::ImportedRaster;
    layer.stage="drawborad.lineart.rgba.v1";layer.sourceResource=resource.id;layer.content={resource.id};layer.bounds={0,0,16,16};
    auto nodes=std::make_shared<NodeMap>(*s.nodes);(*nodes)[layer.id]=layer;nodes->at(Document::rootId()).children.push_back(layer.id);s.nodes=nodes;
    return s;
}
Bytes read(const fs::path& p) {
    std::ifstream f(p,std::ios::binary);
    assert(f.is_open());
    Bytes bytes(std::istreambuf_iterator<char>(f),{});
    assert(!f.bad());
    return bytes;
}
int main(int argc,char** argv){
    for(bool withBucket:{false,true}){
        auto lineart=lineartSample(withBucket);auto encoded=encodeDelta({},lineart);assert(encoded);
        binary::Reader reader{encoded.value()};assert(reader.text(32)=="DRAW-DELTA-9");
        auto restored=decodeDelta(encoded.value(),{});assert(restored && restored.value().sameContent(lineart));
        assert(*restored.value().resources->begin()->second.bytes==*lineart.resources->begin()->second.bytes);
        for(char wrong:{'1','8','7'}){auto bad=encoded.value();bad[19]=wrong;auto denied=decodeDelta(bad,{});assert(!denied && denied.error().code==ErrorCode::UnsupportedFeature);}
    }

    auto s=sample();
    auto delta=encodeDelta({},s);assert(delta);
    auto unknown=delta.value();
    // Version precedes the six fixed style fields, including empty grain length.
    const auto versionAt=unknown.size()-56;
    assert(unknown[versionAt]==2);unknown[versionAt]=99;
    auto decoded=decodeDelta(unknown,{});
    assert(!decoded && decoded.error().code==ErrorCode::UnsupportedFeature);
    const auto dir=fs::temp_directory_path()/("drawborad-fill-storage-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+
        std::to_string(std::random_device{}()));
    assert(fs::create_directory(dir));
    auto legacy=frameRecord(delta.value());assert(legacy);
    auto framed=frameRecord(unknown);assert(framed);
    write(dir/"unknown.draw",framed.value());write(dir/"unknown.draw.bak",legacy.value());
    ProjectStore store;
    auto recovered=store.recover(dir/"unknown.draw");
    assert(!recovered && recovered.error().code==ErrorCode::UnsupportedFeature);
    write(dir/"orphan.draw.bak",legacy.value());
    auto orphan=store.save(dir/"orphan.draw",s);
    assert(!orphan && !fs::exists(dir/"orphan.draw"));
    write(dir/"journal-orphan.draw.journal",legacy.value());
    auto journalOrphan=store.save(dir/"journal-orphan.draw",s);
    assert(!journalOrphan && !fs::exists(dir/"journal-orphan.draw"));
    auto after=s;after.revision=1;
    auto changed=std::make_shared<EventList>(*s.eventIndex);
    changed->front().color.channels[0]=1;after.eventIndex=changed;
    auto journal=encodeDelta(s,after);assert(journal);
    auto futureJournal=journal.value();futureJournal[futureJournal.size()-56]=99;
    assert(store.save(dir/"future-journal.draw",s));
    write(dir/"future-journal.draw.journal",frameRecord(futureJournal).value());
    auto future=store.recover(dir/"future-journal.draw");
    assert(!future && future.error().code==ErrorCode::UnsupportedFeature);
    auto bucket=bucketSample();
    auto bucketDelta=encodeDelta({},bucket);assert(bucketDelta);
    auto roundTrip=decodeDelta(bucketDelta.value(),{});
    assert(roundTrip && roundTrip.value().sameContent(bucket));
    assert(store.save(dir/"legacy.draw",s));
    const auto legacyBytes=read(dir/"legacy.draw");
    bucket.revision=1;
    assert(!store.save(dir/"legacy.draw",bucket));
    assert(read(dir/"legacy.draw")==legacyBytes);
    assert(store.save(dir/"bucket.draw",bucket));
    auto fromDisk=store.recover(dir/"bucket.draw");
    assert(fromDisk && fromDisk.value().snapshot.sameContent(bucket));
    const auto bucketBytes=read(dir/"bucket.draw");
    s.revision=2;
    assert(!store.save(dir/"bucket.draw",s));
    assert(read(dir/"bucket.draw")==bucketBytes);
    bucket.revision=2;
    assert(store.save(dir/"bucket.draw",bucket));
    assert(store.recover(dir/"bucket.draw"));
    write(dir/"bucket.draw.bak",legacy.value());
    assert(!store.recover(dir/"bucket.draw"));
    assert(!store.save(dir/"bucket.draw",bucket));
    auto oldBase=sample();auto changedBucket=bucketSample();changedBucket.revision=1;
    assert(!store.appendJournal(dir/"legacy.draw",oldBase,changedBucket));
    assert(!fs::exists(dir/"legacy.draw.journal"));
    assert(store.save(dir/"clean-bucket.draw",bucket));
    write(dir/"clean-bucket.draw.journal",legacy.value());
    assert(!store.recover(dir/"clean-bucket.draw"));
    binary::Reader header{bucketDelta.value()};
    assert(header.text(32)=="DRAW-DELTA-8");
    auto newFormat=bucketDelta.value();newFormat[19]='7';
    auto unsupportedFormat=decodeDelta(newFormat,{});
    assert(!unsupportedFormat && unsupportedFormat.error().code==ErrorCode::UnsupportedFeature);
    assert(store.save(dir/"v8-journal.draw",bucket));
    auto nextBucket=bucket;nextBucket.revision=3;
    auto nextEvents=std::make_shared<EventList>(*bucket.eventIndex);
    nextEvents->front().color.channels[0]=1;nextBucket.eventIndex=nextEvents;
    assert(store.appendJournal(dir/"v8-journal.draw",bucket,nextBucket));
    auto replay=store.recover(dir/"v8-journal.draw");
    assert(replay && replay.value().snapshot.sameContent(nextBucket));
    assert(store.save(dir/"v8-journal.draw",nextBucket));
    assert(!fs::exists(dir/"v8-journal.draw.journal"));
    assert(store.recover(dir/"v8-journal.draw"));
    auto lineart=lineartSample();lineart.revision=4;
    assert(store.save(dir/"v9.draw",lineart));const auto lineartBytes=read(dir/"v9.draw");
    for(const auto& older:{sample(),bucketSample()}){
        assert(!store.save(dir/"v9.draw",older));assert(read(dir/"v9.draw")==lineartBytes);
        const auto path=dir/(older.eventIndex->front().engineVersion==8?"old8.draw":"old2.draw");
        assert(store.save(path,older));const auto before=read(path);
        assert(!store.save(path,lineart));assert(read(path)==before);
        assert(!store.appendJournal(path,older,lineart));assert(!fs::exists(path.string()+".journal"));
        auto downgrade=older;downgrade.revision=lineart.revision+1;
        assert(!store.appendJournal(dir/"v9.draw",lineart,downgrade));assert(!fs::exists(dir/"v9.draw.journal"));
        write(path.string()+".bak",lineartBytes);assert(!store.recover(path));assert(read(path)==before);fs::remove(path.string()+".bak");
        write(path.string()+".journal",lineartBytes);assert(!store.recover(path));assert(read(path)==before);fs::remove(path.string()+".journal");
        auto olderFrame=frameRecord(encodeDelta({},older).value()).value();
        write(dir/"v9.draw.bak",olderFrame);assert(!store.recover(dir/"v9.draw"));assert(!store.save(dir/"v9.draw",lineart));
        assert(read(dir/"v9.draw")==lineartBytes);fs::remove(dir/"v9.draw.bak");
        write(dir/"v9.draw.journal",olderFrame);assert(!store.recover(dir/"v9.draw"));fs::remove(dir/"v9.draw.journal");
    }
    auto afterLineart=lineart;afterLineart.revision++;
    assert(store.appendJournal(dir/"v9.draw",lineart,afterLineart));assert(store.recover(dir/"v9.draw"));
    assert(store.save(dir/"v9.draw",afterLineart));assert(store.recover(dir/"v9.draw"));
    auto mislabeled=encodeDelta({},lineart).value();mislabeled[19]='8';
    write(dir/"mislabeled.draw",frameRecord(mislabeled).value());write(dir/"mislabeled.draw.bak",legacy.value());
    auto mismatch=store.recover(dir/"mislabeled.draw");assert(!mismatch && mismatch.error().code==ErrorCode::UnsupportedFeature);
    assert(!store.save(dir/"mislabeled.draw",lineart));assert(read(dir/"mislabeled.draw")==frameRecord(mislabeled).value());
    auto futureLineart=encodeDelta({},lineart).value();futureLineart[19]='7';
    write(dir/"future9.draw",frameRecord(futureLineart).value());write(dir/"future9.draw.bak",legacy.value());
    auto future9=store.recover(dir/"future9.draw");assert(!future9 && future9.error().code==ErrorCode::UnsupportedFeature);
    assert(!store.save(dir/"future9.draw",lineart));assert(read(dir/"future9.draw")==frameRecord(futureLineart).value());
    if(argc==2){
        const auto primary=fs::path(argv[1]);
        const auto bytes=frameRecord(bucketDelta.value()).value();
        write(primary,bytes);write(primary.string()+".bak",bytes);
    }
    fs::remove_all(dir);
    std::cout<<"fill storage tests passed\n";
}
