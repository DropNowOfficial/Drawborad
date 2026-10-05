#pragma once
#include "DisplayCache.h"
namespace drawborad {
struct MemoryPressure { std::uint64_t ramUsed{},ramAvailable{},gpuUsed{},gpuBudget{},workerGpuReserved{}; };
struct CacheTrimResult { std::uint64_t tileBytesFreed{}, displayBudget{}; bool cpuFallback{}; };
struct GraphicsResources { std::uint64_t generation{1}; bool available{true}; };
CacheTrimResult trimForPressure(TileCache&,DisplayCache&,const MemoryPressure&,const ResourceBudget&);
Result<void> recoverGraphicsDevice(GraphicsResources&,DisplayCache&,bool canRecreate,const DocumentSnapshot&);
}
