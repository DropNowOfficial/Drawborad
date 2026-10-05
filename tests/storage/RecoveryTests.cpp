#include "Test.h"
#include "core/Commands.h"
#include "storage/ProjectStore.h"
#include "storage/TileStore.h"
#include <fstream>
using namespace drawborad;
namespace fs = std::filesystem;
struct Temp {
    fs::path p;
    Temp() {
        static unsigned i = 0;
        p = fs::temp_directory_path() /
            ("drawborad-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(i++));
        fs::create_directories(p);
    }
    ~Temp() {
        std::error_code e;
        fs::remove_all(p, e);
    }
};
static Document doc() {
    NewDocumentRequest r;
    r.dimensions = {100, 200, Unit::Px, 300};
    auto d = Document::create(validateDocument(r, {}).value(), Id::fromNumber(800)).value();
    LayerNode n;
    n.id = Id::fromNumber(2);
    n.parent = Document::rootId();
    n.name = "Layer";
    CHECK(d.apply(Command{AddNode{n}}, d.snapshot().revision));
    return d;
}
static void rename(Document& d, const char* name) {
    CHECK(d.apply(Command{RenameNode{Id::fromNumber(2), name}}, d.snapshot().revision));
}
static Bytes text(const char* s) {
    return Bytes(s, s + std::char_traits<char>::length(s));
}
struct FaultIO : NativeFileIO {
    int fail = 0;
    Result<void> write(const fs::path& p, std::span<const std::uint8_t> b, bool ex) override {
        if (fail == 1)
            return error(ErrorCode::IoError, "injected.disk-full");
        return NativeFileIO::write(p, b, ex);
    }
    Result<void> flush(const fs::path& p) override {
        if (fail == 2)
            return error(ErrorCode::IoError, "injected.flush");
        return NativeFileIO::flush(p);
    }
    Result<void> replace(const fs::path& a, const fs::path& b) override {
        if (fail == 3)
            return error(ErrorCode::IoError, "injected.sharing-or-readonly");
        return NativeFileIO::replace(a, b);
    }
};
TEST("SHA256 known vectors") {
    CHECK(sha256({}).hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256(text("abc")).hex() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Bytes m(1000000, 'a');
    CHECK(sha256(m).hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
TEST("tile roundtrip CMYKA16 padded layout dedup tamper") {
    Temp tmp;
    TileStore st(tmp.p);
    TileData t;
    t.format = {ColorMode::CMYK, Depth::U16, true};
    t.profileId.bytes[0] = 8;
    t.width = 2;
    t.height = 2;
    t.rowStride = 24;
    t.bytes = Bytes(48, 12);
    auto a = st.put(t);
    CHECK(a);
    CHECK(st.put(t).value() == a.value());
    CHECK(st.get(a.value()).value() == t);
    auto f = tmp.p / (a.value().hex() + ".tile");
    std::ofstream(f, std::ios::binary | std::ios::trunc) << "broken";
    CHECK(!st.get(a.value()));
    t.width = 513;
    CHECK(!st.put(t));
    t.width = 2;
    t.rowStride = 1;
    CHECK(!st.put(t));
}
TEST("project preserves resources events and unicode without source or model") {
    Temp tmp;
    auto p = tmp.p / fs::path(u8"\u5de5\u7a0b\U0001f3a8.draw");
    auto d = doc();
    Bytes data{1, 2, 3, 0, 4};
    Hash id = sha256(data);
    CHECK(d.apply(Command{InstallResource{Resource{
                      id, ResourceKind::Profile, std::make_shared<const Bytes>(data), {}}}},
                  d.snapshot().revision));
    ProjectStore store;
    CHECK(store.save(p, d.snapshot()));
    auto r = store.recover(p);
    CHECK(r);
    CHECK(stateHash(r.value().snapshot).value() == stateHash(d.snapshot()).value());
    CHECK(r.value().snapshot.resources->at(id).bytes->size() == 5);
}
TEST("atomic save failures preserve exact original bytes") {
    Temp tmp;
    auto p = tmp.p / "a.draw";
    auto io = std::make_shared<FaultIO>();
    ProjectStore store(io);
    auto d = doc();
    CHECK(store.save(p, d.snapshot()));
    auto before = io->read(p, MaxProjectBytes).value();
    rename(d, "new");
    for (int f : {1, 2, 3}) {
        io->fail = f;
        CHECK(!store.save(p, d.snapshot()));
        CHECK(io->read(p, MaxProjectBytes).value() == before);
    }
    io->fail = 0;
    CHECK(store.recover(p));
}
TEST("incremental journal torn tails keep last committed state") {
    Temp tmp;
    auto p = tmp.p / "journal.draw";
    ProjectStore store;
    auto d = doc();
    CHECK(store.save(p, d.snapshot()));
    auto a = d.snapshot();
    rename(d, "two");
    auto b = d.snapshot();
    CHECK(store.appendJournal(p, a, b));
    rename(d, "three");
    CHECK(store.appendJournal(p, b, d.snapshot()));
    auto jp = fs::path(p.string() + ".journal");
    auto bytes = NativeFileIO{}.read(jp, MaxProjectBytes).value();
    CHECK(bytes.size() > 16);
    CHECK(NativeFileIO{}.write(jp, std::span(bytes).first(bytes.size() - 13), false));
    auto rr = store.recover(p);
    CHECK(rr);
    CHECK(rr.value().lastCommittedRevision == b.revision);
    CHECK(rr.value().snapshot.sameContent(b));
    CHECK(!rr.value().warnings.empty());
}
TEST("backup fallback and bounded parser") {
    Temp tmp;
    auto p = tmp.p / "x.draw";
    ProjectStore st;
    auto d = doc();
    CHECK(st.save(p, d.snapshot()));
    auto before = d.snapshot();
    rename(d, "v2");
    CHECK(st.save(p, d.snapshot()));
    CHECK(NativeFileIO{}.write(p, text("corrupt"), false));
    auto r = st.recover(p);
    CHECK(r);
    CHECK(r.value().snapshot.sameContent(before));
    CHECK(!r.value().warnings.empty());
    Bytes bomb(64, 255);
    CHECK(!readRecord(bomb, 0));
}
TEST("path escape and interprocess lease fail closed") {
    Temp tmp;
    CHECK(!confinedPath(tmp.p, "../outside"));
    CHECK(!confinedPath(tmp.p, fs::absolute(tmp.p) / "x"));
    FileLease a, b;
    CHECK(a.acquire(tmp.p / "lease"));
    CHECK(!b.acquire(tmp.p / "lease"));
}
TEST("autosave only after thirty seconds and committed changes") {
    Temp tmp;
    auto p = tmp.p / "auto.draw";
    ProjectStore st;
    Autosaver timer;
    auto d = doc();
    CHECK(!timer.tick(29, st, p, d.snapshot()).value());
    CHECK(timer.tick(30, st, p, d.snapshot()).value());
    CHECK(!timer.tick(60, st, p, d.snapshot()).value());
    rename(d, "changed");
    CHECK(timer.tick(61, st, p, d.snapshot()).value());
    CHECK(st.recover(p).value().snapshot.sameContent(d.snapshot()));
}
TEST("history budget never evicts current or protected versions") {
    std::vector<HistoryEntry> v = {{"old", 100, 1, false, false},
                                   {"pin", 100, 2, true, false},
                                   {"current", 100, 3, false, true}};
    auto x = historyEvictions(v, 200);
    CHECK(x);
    CHECK(x.value() == std::vector<fs::path>{"old"});
    CHECK(!historyEvictions(v, 100));
}
TEST("truncation at every payload byte never becomes success") {
    auto d = doc();
    auto b = encodeDelta(DocumentSnapshot{}, d.snapshot());
    CHECK(b);
    for (std::size_t i = 0; i < b.value().size(); ++i)
        CHECK(!decodeDelta(std::span(b.value()).first(i), DocumentSnapshot{}));
}
struct FinalReplaceFail : NativeFileIO {
    fs::path target;
    bool fail = false;
    Result<void> replace(const fs::path& a, const fs::path& b) override {
        if (fail && b == target)
            return error(ErrorCode::IoError, "injected.last-commit-failure");
        return NativeFileIO::replace(a, b);
    }
};
TEST("repair failure never overwrites the only valid backup with corrupt bytes") {
    Temp tmp;
    auto p = tmp.p / "repair.draw";
    auto io = std::make_shared<FinalReplaceFail>();
    io->target = p;
    ProjectStore st(io);
    auto d = doc();
    CHECK(st.save(p, d.snapshot()));
    auto first = d.snapshot();
    rename(d, "second");
    CHECK(st.save(p, d.snapshot()));
    CHECK(io->write(p, text("corrupt primary"), false));
    io->fail = true;
    CHECK(!st.save(p, d.snapshot()));
    io->fail = false;
    auto r = st.recover(p);
    CHECK(r);
    CHECK(r.value().snapshot.sameContent(first));
}
TEST("save rejects conflicting content at an identical revision") {
    Temp tmp;
    auto p = tmp.p / "revision.draw";
    ProjectStore st;
    auto d = doc();
    auto a = d.snapshot();
    CHECK(st.save(p, a));
    auto alternate = Document::fromSnapshot(a).value();
    rename(alternate, "different");
    auto b = alternate.snapshot();
    b.revision = a.revision;
    CHECK(st.save(p, b).error().code == ErrorCode::StaleRevision);
    CHECK(st.recover(p).value().snapshot.sameContent(a));
}
TEST("autosaver saves a brand new revision zero document") {
    Temp tmp;
    auto p = tmp.p / "empty.draw";
    NewDocumentRequest r;
    r.dimensions = {16, 32, Unit::Px, 300};
    auto d = Document::create(validateDocument(r, {}).value(), Id::fromNumber(901)).value();
    Autosaver timer;
    ProjectStore st;
    CHECK(timer.tick(30, st, p, d.snapshot()).value());
    CHECK(st.recover(p));
}
TEST("binary project retains pressure zero tilted strokes and ICC bytes") {
    Temp tmp;
    auto p = tmp.p / "stroke.draw";
    auto d = doc();
    StrokeEvent e;
    e.id = Id::fromNumber(920);
    e.layerId = Id::fromNumber(2);
    e.toolId = "graphite";
    e.parentRevision = d.snapshot().revision;
    e.endUs = 19;
    e.seed = 93817;
    e.geometry = {{1, 2, 0, 15., -25., 0, 1, 7}, {3, 4, .75, 20., -30., 19, 1, 7}};
    CHECK(d.apply(Command{Paint{e, {0, 0, 20, 20}}}, d.snapshot().revision));
    ProjectStore st;
    CHECK(st.save(p, d.snapshot()));
    auto r = st.recover(p);
    CHECK(r);
    CHECK(*r.value().snapshot.eventIndex == *d.snapshot().eventIndex);
    CHECK(r.value().snapshot.eventIndex->front().geometry.front().pressure == 0);
}
