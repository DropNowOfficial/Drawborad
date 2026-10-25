#include "core/Digest.h"
#include "jobs/WinWorkerHost.h"
#include "storage/ProjectCodec.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace drawborad;
int main(int argc, char** argv) {
#ifndef _WIN32
    (void)argc;
    (void)argv;
    std::cout << "BLOCKED: Win32 named pipe, read-only mapping and Job Object require Windows\n";
    return 77;
#else
    if (argc != 2) {
        std::cerr << "worker path required\n";
        return 2;
    }
    NewDocumentRequest dimensions;dimensions.dimensions={1,1,Unit::Px,300};
    const auto spec=validateDocument(dimensions,{});if(!spec)return 1;
    auto document=Document::create(spec.value(),Id::fromNumber(100));if(!document)return 1;
    const std::string metadata=R"({"type":"drawborad.review-request.v1","image":"missing.png","package":"missing","outputDirectory":"."})";
    const Bytes metadataBytes(metadata.begin(),metadata.end());
    Resource reference{sha256(metadataBytes),ResourceKind::Reference,std::make_shared<const Bytes>(metadataBytes),{}};
    if(!document.value().apply(Command{InstallResource{reference}},0))return 1;
    const auto sealed=encodeDelta({},document.value().snapshot());if(!sealed)return 1;
    // Untrusted project metadata alone must not dispatch an inference task.
    for(const auto& payload:std::vector<Bytes>{{1,2,3,4,5},sealed.value()}){
    WinWorkerHost host(std::filesystem::absolute(argv[1]));
    JobRequest r;
    r.id = Id::fromNumber(12);
    r.nonce = Id::fromNumber(78);
    r.task = "native.smoke";
    r.snapshotBytes = payload;
    auto start = host.start(r);
    if (!start)
        return 1;
    bool hello = false, ack = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        auto m = host.pollMessage(r.id);
        if (!m)
            return 1;
        if (m.value()) {
            if (m.value()->kind == MessageKind::Hello) {
                auto h = sha256(r.snapshotBytes);
                if (m.value()->payload != Bytes(h.bytes.begin(), h.bytes.end()))
                    return 1;
                hello = true;
                host.requestCancel(r.id);
            }
            if (m.value()->kind == MessageKind::Cancelled) {
                ack = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host.terminate(r.id);
    if (!hello || !ack)
        return 1;
    }
    std::cout << "PASS: native read-only snapshot hash, nonce and cooperative cancellation\n";
    return 0;
#endif
}
