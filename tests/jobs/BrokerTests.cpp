#include "Test.h"
#include "jobs/Protocol.h"
#include "storage/ProjectCodec.h"
using namespace drawborad;
struct FakeClock : Clock {
    std::uint64_t now = 0;
    std::uint64_t millis() const override {
        return now;
    }
};
struct FakeHost : WorkerHost {
    unsigned starts = 0, cancels = 0, kills = 0;
    bool fail = false;
    Result<void> start(const JobRequest&) override {
        ++starts;
        if (fail)
            return error(ErrorCode::IoError, "fixture.launch-failed");
        return {};
    }
    void requestCancel(JobId) override {
        ++cancels;
    }
    void terminate(JobId) override {
        ++kills;
    }
};
static Document doc() {
    NewDocumentRequest r;
    r.dimensions = {32, 32, Unit::Px, 300};
    auto d = Document::create(validateDocument(r, {}).value(), Id::fromNumber(44)).value();
    return d;
}
static JobRequest request(Document& d, int n = 1) {
    auto s = d.snapshot();
    JobRequest q;
    q.id = Id::fromNumber(n);
    q.nonce = Id::fromNumber(990 + n);
    q.task = "native.smoke";
    q.inputSnapshot = {s.documentId, s.revision, stateHash(s).value()};
    q.resources = {q.id, ResourceDevice::GpuInference, GiB, GiB, 0, true};
    q.snapshotBytes = encodeDelta(DocumentSnapshot{}, s).value();
    return q;
}
static JobResult output(const JobRequest& q, Document& d) {
    auto copy = Document::fromSnapshot(d.snapshot()).value();
    LayerNode n;
    n.id = Id::fromNumber(100);
    n.parent = Document::rootId();
    n.name = "worker-layer";
    CHECK(copy.apply(Command{AddNode{n}}, copy.snapshot().revision));
    auto delta = encodeDelta(d.snapshot(), copy.snapshot()).value();
    JobResult r;
    r.id = q.id;
    r.nonce = q.nonce;
    r.inputRevision = q.inputSnapshot.revision;
    r.delta = delta;
    r.outputHash = sha256(delta);
    return r;
}
TEST("control messages bounded versioned and checksummed") {
    Message m;
    m.id = Id::fromNumber(1);
    m.nonce = Id::fromNumber(2);
    m.payload = {1, 2, 3};
    auto bytes = encodeMessage(m);
    CHECK(bytes);
    CHECK(decodeMessage(bytes.value()).value().payload == m.payload);
    bytes.value().back() ^= 1;
    CHECK(!decodeMessage(bytes.value()));
    m.version = 9;
    CHECK(!encodeMessage(m));
    m.version = 1;
    m.payload.resize(MaxControlBytes + 1);
    CHECK(!encodeMessage(m));
    Bytes truncated(10, 0);
    CHECK(!decodeMessage(truncated));
}
TEST("only one heavy GPU task and shared resource budgets") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    CHECK(b.submit(q));
    CHECK(g.concurrentHeavyGpuJobs() == 1);
    auto q2 = request(d, 2);
    CHECK(!b.submit(q2));
    CHECK(g.concurrentHeavyGpuJobs() == 1);
    g.updateHeadroom(GiB, GiB / 2);
    CHECK(!g.admit(ResourceRequest{Id::fromNumber(3), ResourceDevice::GpuDisplay, GiB, 0, 0, false})
               .admit);
}
TEST("successful output commits exactly once") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    CHECK(b.submit(q));
    auto r = output(q, d);
    auto before = d.snapshot();
    CHECK(b.accept(r, d));
    CHECK(d.snapshot().revision == before.revision + 1);
    CHECK(d.snapshot().nodes->contains(Id::fromNumber(100)));
    CHECK(!b.accept(r, d));
    CHECK(g.concurrentHeavyGpuJobs() == 0);
}
TEST("stale or undo-late output cannot overwrite current revision") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    auto r = output(q, d);
    CHECK(b.submit(q));
    CHECK(d.apply(Command{RenameNode{Document::rootId(), "edit"}}, d.snapshot().revision));
    CHECK(d.undo());
    auto before = d.snapshot();
    auto a = b.accept(r, d);
    CHECK(!a);
    CHECK(a.error().code == ErrorCode::StaleRevision);
    CHECK(d.snapshot().sameContent(before));
}
TEST("cancel acknowledges immediately then kills at five seconds") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    auto r = output(q, d);
    CHECK(b.submit(q));
    b.cancel(q.id);
    CHECK(h.cancels == 1);
    CHECK(b.status(q.id) == JobStatus::CancelRequested);
    auto before = d.snapshot();
    CHECK(b.accept(r, d).error().code == ErrorCode::Cancelled);
    CHECK(d.snapshot().sameContent(before));
    c.now = 4999;
    b.poll();
    CHECK(h.kills == 0);
    c.now = 5000;
    b.poll();
    CHECK(h.kills == 1);
    b.poll();
    CHECK(h.kills == 1);
    CHECK(g.concurrentHeavyGpuJobs() == 0);
}
TEST("wrong nonce wrong job and output tamper do not commit") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    auto r = output(q, d);
    CHECK(b.submit(q));
    auto old = d.snapshot();
    auto bad = r;
    bad.id = Id::fromNumber(123);
    CHECK(!b.accept(bad, d));
    bad = r;
    bad.nonce = Id{};
    CHECK(!b.accept(bad, d));
    bad = r;
    bad.delta.back() ^= 1;
    CHECK(b.accept(bad, d).error().code == ErrorCode::DigestMismatch);
    CHECK(d.snapshot().sameContent(old));
    CHECK(d.snapshot().resources->empty());
}
TEST("launch failure crash and cancel acknowledgment release reservations") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    h.fail = true;
    CHECK(!b.submit(q));
    CHECK(g.concurrentHeavyGpuJobs() == 0);
    h.fail = false;
    q = request(d, 2);
    CHECK(b.submit(q));
    b.workerFailed(q.id);
    CHECK(g.concurrentHeavyGpuJobs() == 0);
    q = request(d, 3);
    CHECK(b.submit(q));
    b.cancel(q.id);
    b.acknowledgeCancel(q.id, Id{});
    CHECK(g.concurrentHeavyGpuJobs() == 1);
    b.acknowledgeCancel(q.id, q.nonce);
    CHECK(g.concurrentHeavyGpuJobs() == 0);
}
TEST("terminal stale results release heavy resource reservation") {
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto d = doc();
    auto q = request(d);
    auto r = output(q, d);
    CHECK(b.submit(q));
    CHECK(d.apply(Command{RenameNode{Document::rootId(), "newer"}}, d.snapshot().revision));
    CHECK(!b.accept(r, d));
    CHECK(g.concurrentHeavyGpuJobs() == 0);
}
TEST("worker cannot remove existing immutable event history") {
    auto d = doc();
    LayerNode n;
    n.id = Id::fromNumber(2);
    n.parent = Document::rootId();
    CHECK(d.apply(Command{AddNode{n}}, d.snapshot().revision));
    StrokeEvent e;
    e.id = Id::fromNumber(91);
    e.layerId = n.id;
    e.parentRevision = d.snapshot().revision;
    e.toolId = "pencil";
    e.geometry = {{1, 1, .3, {}, {}, 0, 0, 0}};
    CHECK(d.apply(Command{Paint{e, {0, 0, 4, 4}}}, d.snapshot().revision));
    auto before = d.snapshot();
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto q = request(d);
    CHECK(b.submit(q));
    auto next = before;
    next.revision++;
    next.eventIndex = std::make_shared<const EventList>();
    JobResult out;
    out.id = q.id;
    out.nonce = q.nonce;
    out.inputRevision = before.revision;
    out.delta = encodeDelta(before, next).value();
    out.outputHash = sha256(out.delta);
    CHECK(!b.accept(out, d));
    CHECK(d.snapshot().sameContent(before));
    CHECK(g.concurrentHeavyGpuJobs() == 0);
}
TEST("authenticated corrupt result is terminal and releases resources") {
    auto d = doc();
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto q = request(d);
    CHECK(b.submit(q));
    auto r = output(q, d);
    r.outputHash = Hash{};
    CHECK(!b.accept(r, d));
    CHECK(g.concurrentHeavyGpuJobs() == 0);
    CHECK(b.status(q.id) == JobStatus::Failed);
}
TEST("external event cannot paint a locked layer without changing metadata") {
    auto d = doc();
    LayerNode n;
    n.id = Id::fromNumber(2);
    n.parent = Document::rootId();
    n.locked = true;
    CHECK(d.apply(Command{AddNode{n}}, d.snapshot().revision));
    auto before = d.snapshot();
    auto next = before;
    next.revision++;
    auto ev = std::make_shared<EventList>();
    StrokeEvent e;
    e.id = Id::fromNumber(91);
    e.layerId = n.id;
    e.parentRevision = before.revision;
    e.toolId = "pencil";
    e.geometry = {{1, 1, .3, {}, {}, 0, 0, 0}};
    ev->push_back(e);
    next.eventIndex = ev;
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto q = request(d);
    CHECK(b.submit(q));
    JobResult r;
    r.id = q.id;
    r.nonce = q.nonce;
    r.inputRevision = before.revision;
    r.delta = encodeDelta(before, next).value();
    r.outputHash = sha256(r.delta);
    CHECK(b.accept(r, d).error().code == ErrorCode::Locked);
    CHECK(d.snapshot().sameContent(before));
}
TEST("accepted event-only result invalidates its destination layer") {
    auto d = doc();
    LayerNode n;
    n.id = Id::fromNumber(2);
    n.parent = Document::rootId();
    CHECK(d.apply(Command{AddNode{n}}, d.snapshot().revision));
    auto before = d.snapshot();
    auto after = Document::fromSnapshot(before).value();
    StrokeEvent e;
    e.id = Id::fromNumber(9);
    e.layerId = n.id;
    e.toolId = "pencil";
    e.parentRevision = before.revision;
    e.geometry = {{1, 1, .4, {}, {}, 0, 0, 0}};
    CHECK(after.apply(Command{Paint{e, {0, 0, 4, 4}}}, before.revision));
    ResourceGovernor g;
    FakeHost h;
    FakeClock c;
    JobBroker b(h, g, c);
    auto q = request(d);
    CHECK(b.submit(q));
    JobResult r;
    r.id = q.id;
    r.nonce = q.nonce;
    r.inputRevision = before.revision;
    r.delta = encodeDelta(before, after.snapshot()).value();
    r.outputHash = sha256(r.delta);
    auto out = b.accept(r, d);
    CHECK(out);
    CHECK(!out.value().changedNodes.empty());
    CHECK(out.value().changedNodes.front() == n.id);
    CHECK(!out.value().dirtyRegions.empty());
}
