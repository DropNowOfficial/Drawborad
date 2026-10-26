#include "core/Digest.h"
#include "jobs/Protocol.h"
#include <cstring>
#include <iostream>
#include <mutex>
#ifdef DRAWBORAD_HAS_REVIEW_WORKER
#include "ui/ReviewJob.h"
#include "ui/ReconstructionJob.h"
#include "storage/ProjectCodec.h"
#include <QSaveFile>
#include <QDir>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
using namespace drawborad;
#ifdef _WIN32
static Id parseId(const std::string& s) {
    Id id;
    if (s.size() != 32)
        throw std::invalid_argument("id length");
    for (std::size_t i = 0; i < 16; i++) {
        auto a = s.substr(i * 2, 2);
        std::size_t n = 0;
        auto v = std::stoul(a, &n, 16);
        if (n != 2)
            throw std::invalid_argument("id hex");
        id.bytes[i] = std::uint8_t(v);
    }
    return id;
}
static int pipeWorker(int argc, char** argv) {
    bool reviewTask=false;
    bool paintTask=false;
    bool modelTask=false;
#ifdef DRAWBORAD_HAS_REVIEW_WORKER
    reviewTask=argc==11 && std::string(argv[9])=="--task" && std::string(argv[10])=="image.review.sam21";
    paintTask=argc==11 && std::string(argv[9])=="--task" && (std::string(argv[10])=="image.reconstruct.v1" || std::string(argv[10])=="image.refine.v1");
    modelTask=argc==11 && std::string(argv[9])=="--task" && std::string(argv[10])=="model.preflight.v1";
#endif
    if (argc != 9 && !reviewTask && !paintTask && !modelTask)
        return 2;
    std::string pipename = argv[2];
    if (pipename.rfind("\\\\.\\pipe\\Drawborad-", 0) != 0 || std::string(argv[3]) != "--mapping" ||
        std::string(argv[5]) != "--id" || std::string(argv[7]) != "--nonce")
        return 2;
    Id id = parseId(argv[6]), nonce = parseId(argv[8]);
    HANDLE mapping = reinterpret_cast<HANDLE>(std::uintptr_t(std::stoull(argv[4])));
    auto memory = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!memory)
        return 3;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(memory, &mi, sizeof(mi)) || mi.RegionSize < 8) {
        UnmapViewOfFile(memory);
        return 3;
    }
    std::uint64_t n = 0;
    std::memcpy(&n, memory, 8);
    if (n > MaxSnapshotMappingBytes || n + 8 > mi.RegionSize) {
        UnmapViewOfFile(memory);
        return 3;
    }
    Hash digest = sha256({static_cast<const std::uint8_t*>(memory) + 8, std::size_t(n)});
#ifdef DRAWBORAD_HAS_REVIEW_WORKER
    Bytes snapshotBytes(static_cast<const std::uint8_t*>(memory)+8,
                        static_cast<const std::uint8_t*>(memory)+8+std::size_t(n));
#endif
    UnmapViewOfFile(memory);
    CloseHandle(mapping);
    HANDLE pipe = CreateFileA(pipename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return 3;
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    std::mutex sendMutex;
    auto send = [&](MessageKind kind, Bytes payload) {
        std::lock_guard lock(sendMutex);
        auto b = encodeMessage({ProtocolVersion, kind, id, nonce, std::move(payload)});
        if (!b)
            return false;
        DWORD wrote = 0;
        return WriteFile(pipe, b.value().data(), DWORD(b.value().size()), &wrote, nullptr) &&
               wrote == b.value().size();
    };
    if (!send(MessageKind::Hello, Bytes(digest.bytes.begin(), digest.bytes.end()))) {
        CloseHandle(pipe);
        return 3;
    }
#ifdef DRAWBORAD_HAS_REVIEW_WORKER
    auto snapshot=decodeDelta(snapshotBytes,DocumentSnapshot{});
    std::optional<QJsonObject> request;
    if((reviewTask || paintTask || modelTask) && snapshot){
        const auto verified=jobRequestMetadata(snapshot.value(),modelTask?ModelPreflightRequestType:paintTask?ReconstructionRequestType:ReviewRequestType);
        if(verified)request=verified.value();
    }
    if((reviewTask || paintTask || modelTask) && !request){send(MessageKind::Failure,{});CloseHandle(pipe);return 3;}
    if(paintTask && request->value("mode").toString()!=(std::string(argv[10])=="image.refine.v1"?"refine":"reconstruct")){send(MessageKind::Failure,{});CloseHandle(pipe);return 3;}
    if((reviewTask || paintTask || modelTask) && request){
        const auto directory=request->value("outputDirectory").toString();
        if(directory.isEmpty() || !QDir(directory).exists()){CloseHandle(pipe);return 3;}
        std::atomic<bool> done=false;
        std::jthread analysis([&](std::stop_token stop){
            auto failure=[&](const std::string& key){send(MessageKind::Failure,Bytes(key.begin(),key.end()));};
            try{
                auto progress=[&](unsigned completed,unsigned total){
                    Bytes payload(8);
                    for(unsigned i=0;i<4;++i){payload[i]=std::uint8_t(completed>>(i*8));payload[4+i]=std::uint8_t(total>>(i*8));}
                    if(!send(MessageKind::Progress,std::move(payload))) throw std::runtime_error("review.progress-pipe");
                };
                auto delta=modelTask?runModelPreflightJob(snapshot.value(),stop,progress):paintTask?runReconstructionJob(snapshot.value(),stop,progress):runImageReviewJob(snapshot.value(),stop,progress);
                if(!stop.stop_requested()){
                    if(!delta) failure(delta.error().messageKey);
                    else if(delta.value().size()>MaxSnapshotMappingBytes) failure("review.delta-limit");
                    else{
                        QSaveFile file(QDir(directory).filePath("review.delta"));
                        if(!file.open(QIODevice::WriteOnly) ||
                           file.write(reinterpret_cast<const char*>(delta.value().data()),qint64(delta.value().size()))!=qint64(delta.value().size()) || !file.commit())
                            failure("review.staged-write");
                        else if(!stop.stop_requested()){
                            const auto hash=sha256(delta.value());
                            send(MessageKind::Result,Bytes(hash.bytes.begin(),hash.bytes.end()));
                        }
                    }
                }
            }catch(const std::exception&){if(!stop.stop_requested())failure("review.worker-exception");}
            done.store(true,std::memory_order_release);
        });
        Bytes buffer(MaxControlBytes);
        while(!done.load(std::memory_order_acquire)){
            DWORD available=0;
            if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)){
                analysis.request_stop();analysis.join();CloseHandle(pipe);return 3;
            }
            if(available){
                DWORD read=0;
                if(!ReadFile(pipe,buffer.data(),DWORD(buffer.size()),&read,nullptr)){
                    analysis.request_stop();analysis.join();CloseHandle(pipe);return 3;
                }
                const auto message=decodeMessage(std::span(buffer).first(read));
                if(!message || message.value().id!=id || message.value().nonce!=nonce || message.value().kind!=MessageKind::Cancel){
                    analysis.request_stop();analysis.join();CloseHandle(pipe);return 3;
                }
                analysis.request_stop();analysis.join();
                const auto sent=send(MessageKind::Cancelled,{});CloseHandle(pipe);return sent?0:3;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        analysis.join();CloseHandle(pipe);return 0;
    }
#endif
    Bytes buffer(MaxControlBytes);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), DWORD(buffer.size()), &read, nullptr)) {
            CloseHandle(pipe);
            return 3;
        }
        auto m = decodeMessage(std::span(buffer).first(read));
        if (!m || m.value().id != id || m.value().nonce != nonce) {
            CloseHandle(pipe);
            return 3;
        }
        if (m.value().kind == MessageKind::Cancel) {
            auto sent = send(MessageKind::Cancelled, {});
            CloseHandle(pipe);
            return sent ? 0 : 3;
        }
    }
}
#endif
int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        if (argc > 1 && std::string(argv[1]) == "--pipe")
            return pipeWorker(argc, argv);
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        // Portable protocol probe, not a model or a reconstruction substitute.
        if (argc != 2 || std::string(argv[1]) != "--stdio")
            return 2;
        std::array<std::uint8_t, 8> header{};
        std::cin.read(reinterpret_cast<char*>(header.data()), 8);
        if (!std::cin)
            return 3;
        std::uint64_t n = 0;
        for (int i = 0; i < 8; i++)
            n |= std::uint64_t(header[i]) << (i * 8);
        if (n > MaxControlBytes - 40)
            return 3;
        Bytes input(header.begin(), header.end());
        input.resize(std::size_t(n) + 40);
        std::cin.read(reinterpret_cast<char*>(input.data() + 8), std::streamsize(n + 32));
        if (!std::cin)
            return 3;
        auto m = decodeMessage(input);
        if (!m)
            return 3;
        Message out = m.value();
        out.kind = out.kind == MessageKind::Cancel ? MessageKind::Cancelled : MessageKind::Hello;
        auto b = encodeMessage(out);
        if (!b)
            return 3;
        std::cout.write(reinterpret_cast<const char*>(b.value().data()),
                        std::streamsize(b.value().size()));
        return std::cout ? 0 : 3;
    } catch (const std::exception&) {
        return 3;
    }
}
