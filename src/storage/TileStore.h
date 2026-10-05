#pragma once
#include "FileIO.h"
#include "core/PixelFormat.h"
#include <memory>
namespace drawborad {
class TileStore {
    ProjectPath root_;
    std::shared_ptr<FileIO> io_;

  public:
    explicit TileStore(ProjectPath root,
                       std::shared_ptr<FileIO> io = std::make_shared<NativeFileIO>())
        : root_(std::move(root)), io_(std::move(io)) {}
    Result<TileId> put(const TileData&);
    Result<TileData> get(TileId) const;
};
} // namespace drawborad
