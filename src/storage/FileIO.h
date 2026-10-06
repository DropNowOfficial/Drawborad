#pragma once
#include "core/Types.h"
#include <filesystem>
namespace drawborad {
using ProjectPath = std::filesystem::path;
class FileIO {
  public:
    virtual ~FileIO() = default;
    virtual Result<Bytes> read(const ProjectPath&, std::uint64_t maxBytes) const = 0;
    virtual Result<void> write(const ProjectPath&, std::span<const std::uint8_t>,
                               bool exclusive) = 0;
    virtual Result<void> flush(const ProjectPath&) = 0;
    virtual Result<void> replace(const ProjectPath& temporary, const ProjectPath& destination) = 0;
    // Publish a staged file. When allowReplace is false the implementation must
    // fail atomically if the destination already exists; a pre-check is not sufficient.
    virtual Result<void> publish(const ProjectPath& temporary, const ProjectPath& destination,
                                 bool allowReplace);
};
class NativeFileIO : public FileIO {
  public:
    Result<Bytes> read(const ProjectPath&, std::uint64_t) const override;
    Result<void> write(const ProjectPath&, std::span<const std::uint8_t>, bool) override;
    Result<void> flush(const ProjectPath&) override;
    Result<void> replace(const ProjectPath&, const ProjectPath&) override;
    Result<void> publish(const ProjectPath&, const ProjectPath&, bool allowReplace) override;
};
Result<void> atomicWrite(FileIO&, const ProjectPath&, std::span<const std::uint8_t>);
Result<ProjectPath> confinedPath(const ProjectPath& root, const ProjectPath& relative);
class FileLease {
    void* native_ = nullptr;
    std::intptr_t fd_ = -1;

  public:
    FileLease() = default;
    ~FileLease();
    FileLease(const FileLease&) = delete;
    FileLease& operator=(const FileLease&) = delete;
    Result<void> acquire(const ProjectPath&);
};
} // namespace drawborad
