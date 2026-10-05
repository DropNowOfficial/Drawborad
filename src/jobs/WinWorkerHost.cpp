#include "WinWorkerHost.h"
#include "core/Digest.h"
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <thread>
#ifdef _WIN32
// Windows SDK extension headers require windows.h first.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
// clang-format on
#endif
namespace drawborad {
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring ident(Id id) {
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring s;
    for (auto b : id.bytes) {
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}
struct OwnedSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    ~OwnedSecurity() {
        if (descriptor)
            LocalFree(descriptor);
    }
    bool initialize() {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            return false;
        Handle token(raw);
        DWORD n = 0;
        GetTokenInformation(token.h, TokenUser, nullptr, 0, &n);
        std::vector<std::uint8_t> bytes(n);
        if (!GetTokenInformation(token.h, TokenUser, bytes.data(), n, &n))
            return false;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
            return false;
        std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
        LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                  &descriptor, nullptr))
            return false;
        attributes.lpSecurityDescriptor = descriptor;
        return true;
    }
};
struct NativeJob {
    Handle pipe, connectEvent, readEvent, writeEvent, job, process, map, readMap;
    OVERLAPPED connecting{}, reading{}, writing{};
    bool connected = false, connectPending = false, readPending = false, writePending = false,
         cancelWanted = false, cancelSent = false;
    Bytes rx = Bytes(MaxControlBytes), tx;
    Id nonce;
    JobId id;
    ~NativeJob() {
        if (pipe.h && pipe.h != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe.h, nullptr);
            DWORD n = 0;
            if (connectPending)
                GetOverlappedResult(pipe.h, &connecting, &n, TRUE);
            if (readPending)
                GetOverlappedResult(pipe.h, &reading, &n, TRUE);
            if (writePending)
                GetOverlappedResult(pipe.h, &writing, &n, TRUE);
        }
    }
};
} // namespace
struct WinWorkerHost::Impl {
    std::filesystem::path exe;
    std::map<JobId, std::unique_ptr<NativeJob>> jobs;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::unique_ptr<NativeJob>> retired;
    bool stopping = false;
    std::thread reaper;
    explicit Impl(std::filesystem::path p)
        : exe(std::move(p)), reaper([this] {
              for (;;) {
                  std::unique_ptr<NativeJob> old;
                  {
                      std::unique_lock lock(mutex);
                      wake.wait(lock, [this] { return stopping || !retired.empty(); });
                      if (retired.empty() && stopping)
                          return;
                      old = std::move(retired.front());
                      retired.pop_front();
                  }
                  old.reset();
              }
          }) {}
    void retire(std::unique_ptr<NativeJob> job) {
        if (!job)
            return;
        if (job->job.h)
            TerminateJobObject(job->job.h, 0);
        if (job->pipe.h && job->pipe.h != INVALID_HANDLE_VALUE)
            CancelIoEx(job->pipe.h, nullptr);
        {
            std::lock_guard lock(mutex);
            retired.push_back(std::move(job));
        }
        wake.notify_one();
    }
    ~Impl() {
        for (auto& [id, j] : jobs) {
            (void)id;
            retire(std::move(j));
        }
        jobs.clear();
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_one();
        reaper.join();
    }
};
WinWorkerHost::WinWorkerHost(std::filesystem::path p)
    : impl_(std::make_unique<Impl>(std::move(p))) {}
WinWorkerHost::~WinWorkerHost() = default;
Result<void> WinWorkerHost::start(const JobRequest& r) {
    if (r.task != "native.smoke")
        return error(ErrorCode::UnsupportedFeature, "worker.task-not-implemented");
    if (r.id.empty() || r.nonce.empty() || r.snapshotBytes.size() > MaxSnapshotMappingBytes ||
        impl_->jobs.contains(r.id) || !impl_->exe.is_absolute() ||
        !std::filesystem::is_regular_file(impl_->exe))
        return error(ErrorCode::InvalidInput, "worker.start");
    auto w = std::make_unique<NativeJob>();
    struct OnExit {
        std::function<void()> f;
        ~OnExit() {
            f();
        }
    } onExit{[&] {
        if (w)
            impl_->retire(std::move(w));
    }};
    w->id = r.id;
    w->nonce = r.nonce;
    OwnedSecurity sd;
    if (!sd.initialize())
        return error(ErrorCode::IoError, "worker.acl");
    Id token;
    if (BCryptGenRandom(nullptr, token.bytes.data(), ULONG(token.bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return error(ErrorCode::IoError, "worker.random");
    auto name = L"\\\\.\\pipe\\Drawborad-" + ident(token);
    w->pipe.h = CreateNamedPipeW(
        name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        DWORD(MaxControlBytes), DWORD(MaxControlBytes), 0, &sd.attributes);
    if (w->pipe.h == INVALID_HANDLE_VALUE)
        return error(ErrorCode::IoError, "worker.pipe");
    w->connectEvent.h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    w->readEvent.h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    w->writeEvent.h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!w->connectEvent.h || !w->readEvent.h || !w->writeEvent.h)
        return error(ErrorCode::IoError, "worker.event");
    w->connecting.hEvent = w->connectEvent.h;
    w->reading.hEvent = w->readEvent.h;
    w->writing.hEvent = w->writeEvent.h;
    if (ConnectNamedPipe(w->pipe.h, &w->connecting))
        w->connected = true;
    else {
        auto e = GetLastError();
        if (e == ERROR_PIPE_CONNECTED) {
            w->connected = true;
            SetEvent(w->connectEvent.h);
        } else if (e == ERROR_IO_PENDING)
            w->connectPending = true;
        else
            return error(ErrorCode::IoError, "worker.connect");
    }
    auto size = r.snapshotBytes.size() + 8;
    w->map.h =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DWORD(size), nullptr);
    if (!w->map.h)
        return error(ErrorCode::IoError, "worker.mapping");
    auto mem = MapViewOfFile(w->map.h, FILE_MAP_WRITE, 0, 0, size);
    if (!mem)
        return error(ErrorCode::IoError, "worker.map-view");
    auto len = std::uint64_t(r.snapshotBytes.size());
    std::memcpy(mem, &len, 8);
    if (len)
        std::memcpy(static_cast<char*>(mem) + 8, r.snapshotBytes.data(), r.snapshotBytes.size());
    UnmapViewOfFile(mem);
    if (!DuplicateHandle(GetCurrentProcess(), w->map.h, GetCurrentProcess(), &w->readMap.h,
                         FILE_MAP_READ, TRUE, 0))
        return error(ErrorCode::IoError, "worker.readonly-handle");
    w->job.h = CreateJobObjectW(nullptr, nullptr);
    if (!w->job.h)
        return error(ErrorCode::IoError, "worker.job-object");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY;
    limits.JobMemoryLimit = SIZE_T(r.budget.ramReclaimBytes);
    if (!SetInformationJobObject(w->job.h, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        return error(ErrorCode::IoError, "worker.job-limits");
    SIZE_T needed = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &needed);
    std::vector<std::uint8_t> list(needed);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &needed))
        return error(ErrorCode::IoError, "worker.handle-list");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attrs;
    HANDLE inherited[] = {w->readMap.h};
    bool updated = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                             sizeof(inherited), nullptr, nullptr) != FALSE;
    std::wstring cmd = L"\"" + impl_->exe.native() + L"\" --pipe \"" + name + L"\" --mapping " +
                       std::to_wstring(reinterpret_cast<std::uintptr_t>(w->readMap.h)) + L" --id " +
                       ident(r.id) + L" --nonce " + ident(r.nonce);
    PROCESS_INFORMATION pi{};
    BOOL started =
        updated
            ? CreateProcessW(impl_->exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                             nullptr, impl_->exe.parent_path().c_str(), &startup.StartupInfo, &pi)
            : FALSE;
    DeleteProcThreadAttributeList(attrs);
    if (!started)
        return error(ErrorCode::IoError, "worker.create-process");
    w->process.h = pi.hProcess;
    Handle thread(pi.hThread);
    if (!AssignProcessToJobObject(w->job.h, w->process.h)) {
        TerminateProcess(w->process.h, 1);
        return error(ErrorCode::IoError, "worker.assign-job");
    }
    if (ResumeThread(thread.h) == DWORD(-1))
        return error(ErrorCode::IoError, "worker.resume");
    impl_->jobs.emplace(r.id, std::move(w));
    return {};
}
void WinWorkerHost::requestCancel(JobId id) {
    auto it = impl_->jobs.find(id);
    if (it == impl_->jobs.end())
        return;
    auto& w = *it->second;
    w.cancelWanted = true;
    if (!w.connected || w.writePending || w.cancelSent)
        return;
    auto msg = encodeMessage({ProtocolVersion, MessageKind::Cancel, id, w.nonce, {}});
    if (!msg)
        return;
    w.tx = std::move(msg.value());
    ResetEvent(w.writeEvent.h);
    DWORD count = 0;
    if (!WriteFile(w.pipe.h, w.tx.data(), DWORD(w.tx.size()), &count, &w.writing)) {
        w.writePending = GetLastError() == ERROR_IO_PENDING;
        w.cancelSent = w.writePending;
    } else
        w.cancelSent = true;
}
void WinWorkerHost::terminate(JobId id) {
    auto it = impl_->jobs.find(id);
    if (it == impl_->jobs.end())
        return;
    auto old = std::move(it->second);
    impl_->jobs.erase(it);
    impl_->retire(std::move(old));
}
Result<std::optional<Message>> WinWorkerHost::pollMessage(JobId id) {
    auto it = impl_->jobs.find(id);
    if (it == impl_->jobs.end())
        return error(ErrorCode::InvalidInput, "worker.unknown");
    auto& w = *it->second;
    DWORD n = 0;
    if (!w.connected) {
        if (!GetOverlappedResult(w.pipe.h, &w.connecting, &n, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE)
                return std::optional<Message>{};
            return error(ErrorCode::IoError, "worker.connect-failed");
        }
        w.connected = true;
        w.connectPending = false;
    }
    if (w.cancelWanted && !w.cancelSent)
        requestCancel(id);
    if (w.readPending) {
        if (!GetOverlappedResult(w.pipe.h, &w.reading, &n, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE)
                return std::optional<Message>{};
            return error(ErrorCode::IoError, "worker.read-failed");
        }
        w.readPending = false;
    } else {
        ResetEvent(w.readEvent.h);
        if (!ReadFile(w.pipe.h, w.rx.data(), DWORD(w.rx.size()), &n, &w.reading)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                w.readPending = true;
                return std::optional<Message>{};
            }
            return error(ErrorCode::IoError, "worker.read");
        }
    }
    auto m = decodeMessage(std::span(w.rx).first(n));
    if (!m)
        return m.error();
    if (m.value().nonce != w.nonce || m.value().id != id)
        return error(ErrorCode::InvalidInput, "worker.identity");
    return std::optional<Message>{std::move(m.value())};
}
#else
struct WinWorkerHost::Impl {};
WinWorkerHost::WinWorkerHost(std::filesystem::path) : impl_(std::make_unique<Impl>()) {}
WinWorkerHost::~WinWorkerHost() = default;
Result<void> WinWorkerHost::start(const JobRequest&) {
    return error(ErrorCode::UnsupportedFeature, "worker.windows-required");
}
void WinWorkerHost::requestCancel(JobId) {}
void WinWorkerHost::terminate(JobId) {}
Result<std::optional<Message>> WinWorkerHost::pollMessage(JobId) {
    return error(ErrorCode::UnsupportedFeature, "worker.windows-required");
}
#endif
} // namespace drawborad
