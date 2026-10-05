#include "Test.h"
#include "core/Commands.h"
#include <algorithm>
#include <cmath>
using namespace drawborad;
static Document emptyDoc() {
    NewDocumentRequest r;
    r.dimensions = {512, 512, Unit::Px, 300};
    return Document::create(validateDocument(r, {}).value(), Id::fromNumber(77)).value();
}
static void ok(Document& d, Operation o) {
    Command c;
    c.operations.push_back(std::move(o));
    CHECK(d.apply(c, d.snapshot().revision));
}
static LayerNode node(int id, NodeId parent, NodeKind k) {
    LayerNode n;
    n.id = Id::fromNumber(id);
    n.parent = parent;
    n.kind = k;
    n.name = "node";
    n.objectId = k == NodeKind::Object ? n.id : Id{};
    n.bounds = {0, 0, 40, 30};
    return n;
}
static Document fixture() {
    auto d = emptyDoc();
    for (int i : {2, 5}) {
        ok(d, AddNode{node(i, Document::rootId(), NodeKind::Object)});
        for (int j = 1; j <= 2; j++) {
            auto n = node(i + j, Id::fromNumber(i), NodeKind::Layer);
            n.objectId = Id::fromNumber(i);
            n.stage = j == 1 ? "base" : "shadow";
            ok(d, AddNode{n});
        }
    }
    return d;
}
static auto oprev(Document& d, Operation op) {
    Command c;
    c.operations.push_back(std::move(op));
    return d.apply(c, d.snapshot().revision);
}
TEST("immutable snapshots and reversible metadata") {
    auto d = fixture();
    auto a = d.snapshot();
    ok(d, RenameNode{Id::fromNumber(3), "changed"});
    auto b = d.snapshot();
    CHECK(a.nodes->at(Id::fromNumber(3)).name == "node");
    CHECK(d.undo());
    CHECK(d.snapshot().sameContent(a));
    CHECK(d.snapshot().revision > b.revision);
    CHECK(d.redo());
    CHECK(d.snapshot().sameContent(b));
}
TEST("stale revision and batch rollback") {
    auto d = fixture();
    auto a = d.snapshot();
    Command c;
    c.operations = {RenameNode{Id::fromNumber(3), "must rollback"}, RemoveNode{Id::fromNumber(99)}};
    CHECK(!d.apply(c, a.revision));
    CHECK(d.snapshot().sameContent(a));
    CHECK(d.snapshot().revision == a.revision);
    CHECK(d.apply(Command{RenameNode{Id::fromNumber(3), "x"}}, a.revision - 1).error().code ==
          ErrorCode::StaleRevision);
}
TEST("ownership cycles duplicate IDs and locked ancestors") {
    auto d = fixture();
    CHECK(oprev(d, MoveNode{Id::fromNumber(2), Id::fromNumber(3)}).error().code ==
          ErrorCode::Cycle);
    CHECK(!oprev(d, AddNode{node(3, Document::rootId(), NodeKind::Layer)}));
    SetProperties lock;
    lock.id = Id::fromNumber(2);
    lock.locked = true;
    ok(d, lock);
    CHECK(oprev(d, RenameNode{Id::fromNumber(3), "forbidden"}).error().code == ErrorCode::Locked);
    CHECK(oprev(d, RemoveNode{Id::fromNumber(2)}).error().code == ErrorCode::Locked);
    lock.locked = false;
    ok(d, lock);
    CHECK(oprev(d, RenameNode{Id::fromNumber(3), "allowed"}));
}
TEST("visibility transform and change regions leave other object unchanged") {
    auto d = fixture();
    auto a = d.snapshot();
    SetProperties p;
    p.id = Id::fromNumber(3);
    p.visible = false;
    p.opacity = .4;
    p.transform = LayerTransform{{1, 0, 0, 1, 21, 9}};
    auto c = oprev(d, p);
    CHECK(c);
    CHECK(!c.value().dirtyRegions.empty());
    CHECK(d.snapshot().nodes->at(Id::fromNumber(5)) == a.nodes->at(Id::fromNumber(5)));
    CHECK(d.undo());
    CHECK(d.snapshot().sameContent(a));
    p.transform = LayerTransform{{0, 0, 0, 0, 0, 0}};
    CHECK(!oprev(d, p));
    p.transform.reset();
    p.opacity = NAN;
    CHECK(!oprev(d, p));
}
TEST("reorder duplicate delete merge split retain identity") {
    auto d = fixture();
    ok(d, MoveNode{Id::fromNumber(4), Id::fromNumber(2), 0});
    CHECK(d.snapshot().nodes->at(Id::fromNumber(2)).children.front() == Id::fromNumber(4));
    DuplicateNode dup{Id::fromNumber(2),
                      {{Id::fromNumber(2), Id::fromNumber(20)},
                       {Id::fromNumber(3), Id::fromNumber(21)},
                       {Id::fromNumber(4), Id::fromNumber(22)}}};
    ok(d, dup);
    CHECK(d.snapshot().nodes->at(Id::fromNumber(21)).objectId == Id::fromNumber(20));
    auto a = d.snapshot();
    ok(d, RemoveNode{Id::fromNumber(20)});
    CHECK(d.undo());
    CHECK(d.snapshot().sameContent(a));
    ok(d, MergeNodes{{Id::fromNumber(3), Id::fromNumber(4)}, Id::fromNumber(30), "composite"});
    CHECK(d.snapshot().nodes->at(Id::fromNumber(30)).provenance.size() == 2);
    ok(d, SplitNode{Id::fromNumber(30)});
    CHECK(!d.snapshot().nodes->contains(Id::fromNumber(30)));
    CHECK(d.snapshot().nodes->at(Id::fromNumber(3)).parent == Id::fromNumber(2));
}
TEST("mask resources dependency DAG and source provenance") {
    auto d = fixture();
    Hash a, b;
    a.bytes[0] = 1;
    b.bytes[0] = 2;
    ok(d, InstallResource{
              Resource{a, ResourceKind::Mask, std::make_shared<const Bytes>(Bytes{1, 2, 3}), {}}});
    ok(d, InstallResource{
              Resource{b, ResourceKind::Mask, std::make_shared<const Bytes>(Bytes{4}), {a}}});
    auto before = d.snapshot();
    ok(d, SetMask{Id::fromNumber(3), b});
    CHECK(d.undo());
    CHECK(d.snapshot().sameContent(before));
    CHECK(oprev(d, SetDependencies{a, {b}}).error().code == ErrorCode::Cycle);
    auto n = node(40, Id::fromNumber(2), NodeKind::Layer);
    n.sourceKind = SourceKind::Reconstructed;
    n.sourceResource = a;
    CHECK(!oprev(d, AddNode{n}));
}
TEST("stroke event IDs survive layer reordering zero pressure preserved") {
    auto d = fixture();
    StrokeEvent e;
    e.id = Id::fromNumber(99);
    e.objectId = Id::fromNumber(2);
    e.layerId = Id::fromNumber(3);
    e.parentRevision = d.snapshot().revision;
    e.toolId = "pencil";
    e.endUs = 16;
    e.geometry = {{1, 2, 0, {}, {}, 0, 0, 0}, {3, 4, .5, {}, {}, 16, 0, 0}};
    ok(d, Paint{e, {0, 0, 10, 10}});
    CHECK(d.snapshot().eventIndex->size() == 1);
    CHECK(d.snapshot().eventIndex->front().geometry.front().pressure == 0);
    auto id = d.snapshot().eventIndex->front().id;
    ok(d, MoveNode{Id::fromNumber(3), Id::fromNumber(2), 0});
    CHECK(d.snapshot().eventIndex->front().id == id);
    CHECK(!oprev(d, Paint{e, {0, 0, 10, 10}}));
}
TEST("ten thousand reversible commands with stable state") {
    auto d = fixture();
    auto base = d.snapshot();
    for (int i = 0; i < 10000; ++i)
        ok(d, RenameNode{Id::fromNumber(3), std::to_string(i)});
    for (int i = 0; i < 10000; ++i)
        CHECK(d.undo());
    CHECK(d.snapshot().sameContent(base));
    for (int i = 0; i < 10000; ++i)
        CHECK(d.redo());
    CHECK(d.snapshot().nodes->at(Id::fromNumber(3)).name == "9999");
}
TEST("imported snapshots detach all mutable aliases") {
    auto original = fixture();
    auto s = original.snapshot();
    auto ns = std::make_shared<NodeMap>(*s.nodes);
    auto rs = std::make_shared<ResourceMap>(*s.resources);
    auto ev = std::make_shared<EventList>(*s.eventIndex);
    auto pixels = std::make_shared<Bytes>(Bytes{8, 9});
    Hash id;
    id.bytes[0] = 44;
    rs->emplace(id, Resource{id, ResourceKind::Tile, pixels, {}});
    s.nodes = ns;
    s.resources = rs;
    s.eventIndex = ev;
    auto imported = Document::fromSnapshot(s);
    CHECK(imported);
    auto before = imported.value().snapshot();
    ns->at(Id::fromNumber(3)).name = "external mutation";
    pixels->at(0) = 222;
    ev->push_back(StrokeEvent{});
    CHECK(imported.value().snapshot().nodes->at(Id::fromNumber(3)).name == "node");
    CHECK(imported.value().snapshot().resources->at(id).bytes->at(0) == 8);
    CHECK(imported.value().snapshot().eventIndex->empty());
    CHECK(original.snapshot().eventIndex->empty());
    (void)before;
}
TEST("serialized document descriptors reject overflow and inconsistent estimates") {
    auto s = fixture().snapshot();
    s.spec.pixelSize = {UINT64_MAX, UINT64_MAX};
    CHECK(!Document::fromSnapshot(s));
    s = fixture().snapshot();
    s.spec.estimatedBytes = 1;
    CHECK(!Document::fromSnapshot(s));
    s = fixture().snapshot();
    s.spec.tileCount = 999;
    CHECK(!Document::fromSnapshot(s));
}
TEST("split refuses to discard composite visual properties") {
    auto d = fixture();
    ok(d, MergeNodes{{Id::fromNumber(3), Id::fromNumber(4)}, Id::fromNumber(30), "group"});
    SetProperties p;
    p.id = Id::fromNumber(30);
    p.opacity = .4;
    ok(d, p);
    auto old = d.snapshot();
    CHECK(oprev(d, SplitNode{Id::fromNumber(30)}).error().code == ErrorCode::UnsupportedFeature);
    CHECK(d.snapshot().sameContent(old));
}
TEST("transforming a group cannot mutate a locked descendant") {
    auto d = fixture();
    SetProperties p;
    p.id = Id::fromNumber(3);
    p.locked = true;
    ok(d, p);
    auto before = d.snapshot();
    p = {};
    p.id = Id::fromNumber(2);
    p.transform = LayerTransform{{1, 0, 0, 1, 10, 0}};
    CHECK(oprev(d, p).error().code == ErrorCode::Locked);
    CHECK(d.snapshot().sameContent(before));
}
TEST("explicit color conversion changes format estimates reversibly") {
    auto d = fixture();
    auto before = d.snapshot();
    ColorSpec c;
    c.format = {ColorMode::CMYK, Depth::U16, true};
    c.profileId.bytes[0] = 78;
    ConvertColor op{c, {}, std::make_shared<const EventList>()};
    CHECK(oprev(d, op));
    CHECK(d.snapshot().spec.estimatedBytes == 512ull * 512 * 10);
    CHECK(d.undo());
    CHECK(d.snapshot().sameContent(before));
}
TEST("undo reports IDs of nodes removed by the inverse transaction") {
    auto d = fixture();
    auto id = Id::fromNumber(88);
    ok(d, AddNode{node(88, Id::fromNumber(2), NodeKind::Layer)});
    auto change = d.undo();
    CHECK(change);
    CHECK(std::find(change.value().changedNodes.begin(), change.value().changedNodes.end(), id) !=
          change.value().changedNodes.end());
}
