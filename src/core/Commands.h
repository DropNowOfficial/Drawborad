#pragma once
#include "Document.h"
#include <array>
namespace drawborad {
struct AddNode {
    LayerNode node;
    std::size_t index = SIZE_MAX;
};
struct RemoveNode {
    NodeId id;
};
struct RenameNode {
    NodeId id;
    std::string name;
};
struct MoveNode {
    NodeId id, parent;
    std::size_t index = SIZE_MAX;
};
struct DuplicateNode {
    NodeId id;
    std::map<NodeId, NodeId> idMap;
};
struct SetProperties {
    NodeId id;
    std::optional<bool> visible, locked;
    std::optional<double> opacity;
    std::optional<BlendMode> blend;
    std::optional<LayerTransform> transform;
};
struct SetMask {
    NodeId id;
    Hash mask;
};
struct InstallResource {
    Resource resource;
};
struct SetDependencies {
    Hash id;
    std::vector<Hash> dependencies;
};
struct RecolorStroke {
    Id id;
    ColorValue color;
};
// Only positions/pressure of an eligible smooth-ink event may change.
struct AdjustStrokeSamples {
    Id id;
    std::vector<InputSample> geometry;
};
struct AdjustCubicPathControl { Id id; std::size_t index; double x,y; };
struct AdjustCubicControls { Id id; std::array<std::array<double,2>,4> controls; };
struct CloseStrokeGap { Id eventId; };
struct WithdrawClosedInkFill { Id operationId; };
struct RecordClosedInkFill { FillOperation operation; };
struct Paint {
    StrokeEvent event;
    Rect dirty;
};
struct MergeNodes {
    std::vector<NodeId> ids;
    NodeId groupId;
    std::string name;
};
struct SplitNode {
    NodeId groupId;
};
struct ConvertColor {
    ColorSpec target;
    std::map<NodeId, std::vector<Hash>> replacementContent;
    std::shared_ptr<const EventList> events;
    std::optional<ColorValue> paperColor = {};
};
using Operation =
    std::variant<AddNode, RemoveNode, RenameNode, MoveNode, DuplicateNode, SetProperties, SetMask,
                 InstallResource, SetDependencies, Paint, RecordClosedInkFill, WithdrawClosedInkFill, CloseStrokeGap, RecolorStroke, AdjustStrokeSamples, AdjustCubicControls, AdjustCubicPathControl, MergeNodes, SplitNode, ConvertColor>;
struct Command {
    std::uint32_t version = 1;
    std::vector<Operation> operations;
    Command() = default;
    template <class O> explicit Command(O o) : operations{std::move(o)} {}
};
} // namespace drawborad
