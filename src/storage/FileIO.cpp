#include "FileIO.h"
#include <atomic>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#endif
#endif
namespace drawborad {
namespace fs = std::filesystem;
namespace {
#ifdef _WIN32
std::wstring wp(const fs::path& p) {
    auto s = fs::absolute(p).lexically_normal().native();
    if (s.rfind(L"\\\\?\\", 0) == 0)
        return s;
    if (s.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + s.substr(2);
    return L"\\\\?\\" + s;
}
#endif
Error ioerr(const char* key) {
    return error(ErrorCode::IoError, key);
}
} // namespace

Result<void> FileIO::publish(const fs::path& temporary, const fs::path& destination,
                             bool allowReplace) {
    if (!allowReplace)
        return error(ErrorCode::UnsupportedFeature, "io.no-replace-unsupported");
    return replace(temporary, destination);
}
Result<Hash> FileIO::hash(const fs::path& p, std::uint64_t maxBytes) const {
    auto bytes = read(p, maxBytes);
    if (!bytes)
        return bytes.error();
    return sha256(bytes.value());
}
Result<Bytes> NativeFileIO::read(const fs::path& p, std::uint64_t max) const {
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ioerr("io.open");
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !GetFileSizeEx(h, &size) || size.QuadPart < 0) {
        CloseHandle(h);
        return error(ErrorCode::PathEscape, "io.read-metadata");
    }
    const auto n = static_cast<std::uint64_t>(size.QuadPart);
    if (n > max || n > SIZE_MAX) {
        CloseHandle(h);
        return error(ErrorCode::BudgetExceeded, "io.read-limit");
    }
    Bytes b;
    try {
        b.resize(static_cast<std::size_t>(n));
    } catch (const std::bad_alloc&) {
        CloseHandle(h);
        return error(ErrorCode::BudgetExceeded, "io.read-allocation");
    }
    std::size_t pos = 0;
    while (pos < b.size()) {
        const auto chunk = DWORD(std::min<std::size_t>(b.size() - pos, 1u << 20));
        DWORD got = 0;
        if (!ReadFile(h, b.data() + pos, chunk, &got, nullptr) || got != chunk) {
            CloseHandle(h);
            return ioerr("io.short-or-growing-file");
        }
        pos += got;
    }
    LARGE_INTEGER after{};
    const bool stable = GetFileSizeEx(h, &after) && after.QuadPart == size.QuadPart;
    CloseHandle(h);
    if (!stable)
        return ioerr("io.short-or-growing-file");
    return b;
#else
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    if (ec)
        return ioerr("io.stat");
    if (n > max || n > SIZE_MAX)
        return error(ErrorCode::BudgetExceeded, "io.read-limit");
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return ioerr("io.open");
    Bytes b(static_cast<std::size_t>(n));
    if (n)
        f.read(reinterpret_cast<char*>(b.data()), std::streamsize(n));
    if (!f || f.peek() != std::char_traits<char>::eof())
        return ioerr("io.short-or-growing-file");
    return b;
#endif
}
Result<Hash> NativeFileIO::hash(const fs::path& p, std::uint64_t maxBytes) const {
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ioerr("io.open");
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !GetFileSizeEx(h, &size) || size.QuadPart < 0) {
        CloseHandle(h);
        return error(ErrorCode::PathEscape, "io.hash-metadata");
    }
    const auto expected = static_cast<std::uint64_t>(size.QuadPart);
    if (expected > maxBytes) {
        CloseHandle(h);
        return error(ErrorCode::BudgetExceeded, "io.hash-limit");
    }
    Sha256State state;
    std::vector<std::uint8_t> buffer;
    try {
        buffer.resize(1024 * 1024);
    } catch (const std::bad_alloc&) {
        CloseHandle(h);
        return error(ErrorCode::BudgetExceeded, "io.hash-buffer");
    }
    std::uint64_t total = 0;
    while (total < expected) {
        const auto request =
            DWORD(std::min<std::uint64_t>(buffer.size(), expected - total));
        DWORD got = 0;
        if (!ReadFile(h, buffer.data(), request, &got, nullptr) || got != request) {
            CloseHandle(h);
            return ioerr("io.short-or-growing-file");
        }
        state.update(std::span<const std::uint8_t>(buffer.data(), got));
        total += got;
    }
    LARGE_INTEGER after{};
    const bool stable = GetFileSizeEx(h, &after) && after.QuadPart == size.QuadPart;
    CloseHandle(h);
    if (!stable)
        return ioerr("io.short-or-growing-file");
    return state.finish();
#else
    std::error_code ec;
    const auto expected = fs::file_size(p, ec);
    if (ec)
        return ioerr("io.stat");
    if (expected > maxBytes)
        return error(ErrorCode::BudgetExceeded, "io.hash-limit");
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return ioerr("io.open");
    Sha256State state;
    std::vector<std::uint8_t> buffer;
    try {
        buffer.resize(1024 * 1024);
    } catch (const std::bad_alloc&) {
        return error(ErrorCode::BudgetExceeded, "io.hash-buffer");
    }
    std::uint64_t total = 0;
    while (total < expected) {
        const auto request = std::size_t(std::min<std::uint64_t>(buffer.size(), expected - total));
        in.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(request));
        const auto got = std::size_t(in.gcount());
        if (got != request)
            return ioerr("io.short-or-growing-file");
        state.update(std::span<const std::uint8_t>(buffer.data(), got));
        total += got;
    }
    if (in.peek() != std::char_traits<char>::eof())
        return ioerr("io.short-or-growing-file");
    return state.finish();
#endif
}
Result<void> NativeFileIO::write(const fs::path& p, std::span<const std::uint8_t> bytes,
                                 bool exclusive) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(p, ec)))
        return error(ErrorCode::PathEscape, "io.symlink");
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_WRITE, 0, nullptr,
                           exclusive ? CREATE_NEW : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ioerr("io.create");
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        DWORD n = 0;
        auto chunk = DWORD(std::min<std::size_t>(bytes.size() - pos, 1 << 20));
        if (!WriteFile(h, bytes.data() + pos, chunk, &n, nullptr) || n == 0) {
            CloseHandle(h);
            return ioerr("io.write");
        }
        pos += n;
    }
    CloseHandle(h);
#else
    int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | (exclusive ? O_EXCL : O_TRUNC);
    int fd = ::open(p.c_str(), flags, 0600);
    if (fd < 0)
        return ioerr("io.create");
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        auto n = ::write(fd, bytes.data() + pos, bytes.size() - pos);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ::close(fd);
            return ioerr("io.write");
        }
        pos += std::size_t(n);
    }
    if (::close(fd) != 0)
        return ioerr("io.close");
#endif
    return {};
}
Result<void> NativeFileIO::flush(const fs::path& p) {
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ioerr("io.flush-open");
    auto ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok)
        return ioerr("io.flush");
#else
    int fd = ::open(p.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return ioerr("io.flush-open");
    int r = ::fsync(fd);
    ::close(fd);
    if (r)
        return ioerr("io.flush");
#endif
    return {};
}
Result<void> NativeFileIO::replace(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    const fs::path destination{wp(b)};
#else
    const auto& destination = b;
#endif
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(destination, ec)))
        return error(ErrorCode::PathEscape, "io.symlink");
#ifdef _WIN32
    BOOL ok = fs::exists(destination)
                  ? ReplaceFileW(destination.c_str(), wp(a).c_str(), nullptr, 0, nullptr, nullptr)
                  : MoveFileExW(wp(a).c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!ok)
        return ioerr("io.atomic-replace");
#else
    if (::rename(a.c_str(), b.c_str()) != 0)
        return ioerr("io.atomic-replace");
    auto parent = b.parent_path().empty() ? fs::path(".") : b.parent_path();
    int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return ioerr("io.replace-committed-directory-open");
    int r = ::fsync(fd);
    ::close(fd);
    if (r)
        return ioerr("io.replace-committed-directory-flush");
#endif
    return {};
}

Result<void> NativeFileIO::publish(const fs::path& a, const fs::path& b, bool allowReplace) {
    if (allowReplace)
        return replace(a, b);
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(b, ec)))
        return error(ErrorCode::PathEscape, "io.symlink");
#ifdef _WIN32
    // Deliberately omit MOVEFILE_REPLACE_EXISTING. The kernel checks the
    // destination at the move itself, closing the exists-then-replace race.
    if (!MoveFileExW(wp(a).c_str(), wp(b).c_str(), MOVEFILE_WRITE_THROUGH))
        return ioerr("io.atomic-create");
    return {};
#elif defined(__linux__)
    if (::syscall(SYS_renameat2, AT_FDCWD, a.c_str(), AT_FDCWD, b.c_str(),
                  RENAME_NOREPLACE) != 0)
        return ioerr("io.atomic-create");
    auto parent = b.parent_path().empty() ? fs::path(".") : b.parent_path();
    int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return ioerr("io.create-committed-directory-open");
    int r = ::fsync(fd);
    ::close(fd);
    if (r)
        return ioerr("io.create-committed-directory-flush");
    return {};
#else
    // Product target is Windows. On other POSIX hosts fail closed rather than
    // emulate no-clobber with an racy exists()+rename sequence.
    return error(ErrorCode::UnsupportedFeature, "io.atomic-create-unavailable");
#endif
}
NativeOutputFile::~NativeOutputFile() {
    (void)close();
}
Result<void> NativeOutputFile::createExclusive(const fs::path& p) {
    if (native_ || fd_ >= 0)
        return error(ErrorCode::Locked, "io.output-already-open");
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(p, ec)))
        return error(ErrorCode::PathEscape, "io.symlink");
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return ioerr("io.create");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return error(ErrorCode::PathEscape, "io.output-reparse");
    }
    native_ = h;
#else
    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return ioerr("io.create");
    fd_ = fd;
#endif
    return {};
}
Result<void> NativeOutputFile::write(std::span<const std::uint8_t> bytes) {
    if (!native_ && fd_ < 0)
        return error(ErrorCode::InvalidInput, "io.output-not-open");
    std::size_t pos = 0;
#ifdef _WIN32
    HANDLE h = static_cast<HANDLE>(native_);
    while (pos < bytes.size()) {
        DWORD written = 0;
        const auto chunk = DWORD(std::min<std::size_t>(bytes.size() - pos, 1 << 20));
        if (!WriteFile(h, bytes.data() + pos, chunk, &written, nullptr) || written == 0)
            return ioerr("io.write");
        pos += written;
    }
#else
    while (pos < bytes.size()) {
        const auto n = ::write(int(fd_), bytes.data() + pos, bytes.size() - pos);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return ioerr("io.write");
        pos += std::size_t(n);
    }
#endif
    return {};
}
Result<void> NativeOutputFile::close() {
#ifdef _WIN32
    if (native_) {
        auto h = static_cast<HANDLE>(native_);
        native_ = nullptr;
        if (!CloseHandle(h))
            return ioerr("io.close");
    }
#else
    if (fd_ >= 0) {
        auto fd = int(fd_);
        fd_ = -1;
        if (::close(fd) != 0)
            return ioerr("io.close");
    }
#endif
    return {};
}

Result<void> atomicWrite(FileIO& io, const fs::path& target, std::span<const std::uint8_t> data) {
    static std::atomic<std::uint64_t> n{};
    auto tmp = target;
    tmp += std::string(".tmp-") +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(n++);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove(p, e);
        }
    } cleanup{tmp};
    auto w = io.write(tmp, data, true);
    if (!w)
        return w.error();
    auto f = io.flush(tmp);
    if (!f)
        return f.error();
    return io.replace(tmp, target);
}
Result<fs::path> confinedPath(const fs::path& root, const fs::path& rel) {
    if (rel.empty() || rel.is_absolute() || rel.has_root_name())
        return error(ErrorCode::PathEscape, "path.absolute");
    for (auto& p : rel)
        if (p == ".." || p.string().find(':') != std::string::npos ||
            p.string().find('\\') != std::string::npos)
            return error(ErrorCode::PathEscape, "path.parent");
    std::error_code ec;
    auto r = fs::weakly_canonical(root, ec);
    if (ec)
        return ioerr("path.root");
    auto d = fs::weakly_canonical(r / rel, ec);
    if (ec)
        return ioerr("path.resolve");
    auto rr = d.lexically_relative(r);
    if (rr.empty() || *rr.begin() == "..")
        return error(ErrorCode::PathEscape, "path.symlink");
    return d;
}
FileLease::~FileLease() {
#ifdef _WIN32
    if (native_)
        CloseHandle(static_cast<HANDLE>(native_));
#else
    if (fd_ >= 0) {
        flock(int(fd_), LOCK_UN);
        ::close(int(fd_));
    }
#endif
}
Result<void> FileLease::acquire(const fs::path& p) {
    if (native_ || fd_ >= 0)
        return error(ErrorCode::Locked, "lease.already-held");
#ifdef _WIN32
    HANDLE h = CreateFileW(wp(p).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return error(ErrorCode::Locked, "project.single-writer");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        return error(ErrorCode::PathEscape, "lease.reparse");
    }
    native_ = h;
#else
    int fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return ioerr("lease.open");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        ::close(fd);
        return error(ErrorCode::Locked, "project.single-writer");
    }
    fd_ = fd;
#endif
    return {};
}
} // namespace drawborad
