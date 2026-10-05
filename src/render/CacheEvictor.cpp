#include "GraphicsResources.h"
#include <algorithm>
namespace drawborad {
CacheTrimResult trimForPressure(TileCache& tiles,DisplayCache& display,const MemoryPressure& p,const ResourceBudget& b){CacheTrimResult r;auto appGpu=p.gpuUsed+p.workerGpuReserved;auto remaining=appGpu>=b.gpuTotalBytes?0:b.gpuTotalBytes-appGpu;r.displayBudget=std::min(b.gpuDisplayBytes,remaining);if(p.ramUsed>b.ramReclaimBytes||p.ramAvailable<2*GiB)r.tileBytesFreed=tiles.trimTo(0);if(r.displayBudget==0){display.clear();r.cpuFallback=true;}return r;}
}
