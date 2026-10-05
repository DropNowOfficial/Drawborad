#include "BrushEngine.h"
#include <cmath>
namespace drawborad {
InputSample normalizeTabletSample(const TabletPacket& p){InputSample s;s.x=p.x;s.y=p.y;s.timestampUs=p.timestampUs;s.deviceKind=p.deviceKind;s.capabilities=p.capabilities;s.pressure=p.pressure.value_or(1.0);if(p.capabilities&TiltCap){s.tiltX=p.tiltX;s.tiltY=p.tiltY;}return s;}
}
