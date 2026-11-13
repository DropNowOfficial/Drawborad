#pragma once
#include "core/Document.h"
#include <span>
namespace drawborad {
enum InputCapability : std::uint32_t { PressureCap=1, TiltCap=2, EraserCap=4 };
struct TabletPacket { double x{},y{}; std::optional<double> pressure,tiltX,tiltY; std::uint64_t timestampUs{}; std::uint32_t deviceKind{},capabilities{}; bool eraser{}; };
struct BrushSettings { NodeId objectId,layerId; Revision parentRevision{}; std::string toolId="pencil"; ColorValue color; double width=8,opacity=1,hardness=.8,spacing=.25; std::uint64_t seed{}; NibShape shape=NibShape::Round; };
InputSample normalizeTabletSample(const TabletPacket&);
// rootLevelLayer is opt-in: the caller must verify the target is a root-level
// native layer whose objectId is empty. This does not validate a document stack.
class BrushEngine { public: Result<StrokeEvent> compile(std::span<const InputSample>,const BrushSettings&,bool eraser=false,bool rootLevelLayer=false) const; };
}

