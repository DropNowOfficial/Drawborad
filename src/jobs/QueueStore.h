#pragma once
#include "ImageQueue.h"
#include "storage/FileIO.h"

namespace drawborad {
class QueueStore {
  public:
    static Result<void> save(const ImageQueue&, const std::filesystem::path&);
    static Result<ImageQueue> load(const std::filesystem::path&);
};
}
