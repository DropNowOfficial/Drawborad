#pragma once
#include "brush/BrushEngine.h"
namespace drawborad { struct TabletNormalizer { static InputSample normalize(const TabletPacket& p){return normalizeTabletSample(p);} }; }
