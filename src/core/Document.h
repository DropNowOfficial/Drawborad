#pragma once
#include "DocumentSpec.h"
#include <map>
#include <memory>
#include <optional>
namespace drawborad {
enum class NodeKind { Root, Object, Group, Layer };
enum class SourceKind { ImportedRaster, ReferenceOnly, Reconstructed };
enum class BlendMode { Normal, Multiply, Screen, Darken, Lighten };
enum class ResourceKind { Tile, Mask, Profile, Geometry, Reference };
struct Resource {
    Hash id;
    ResourceKind kind = ResourceKind::Tile;
    std::shared_ptr<const Bytes> bytes = std::make_shared<const Bytes>();
    std::vector<Hash> dependencies;
    bool operator==(const Resource& r) const {
        return id == r.id && kind == r.kind && *bytes == *r.bytes && dependencies == r.dependencies;
    }
};
struct LayerNode {
    NodeId id, parent, objectId;
    NodeKind kind = NodeKind::Layer;
    std::string name, stage;
    Hash maskId, sourceResource;
    std::vector<Hash> content;
    LayerTransform transform;
    double opacity = 1;
    BlendMode blend = BlendMode::Normal;
    bool visible = true, locked = false;
    SourceKind sourceKind = SourceKind::Reconstructed;
    Rect bounds;
    std::vector<NodeId> children, provenance;
    bool composite = false;
    bool operator==(const LayerNode&) const = default;
};
struct InputSample {
    double x{}, y{}, pressure{};
    std::optional<double> tiltX, tiltY;
    std::uint64_t timestampUs{};
    std::uint32_t deviceKind{}, capabilities{};
    bool operator==(const InputSample&) const = default;
};
struct StrokeEvent {
    Id id;
    NodeId objectId, layerId;
    std::uint64_t startUs{}, endUs{};
    std::string toolId;
    ColorValue color;
    std::vector<InputSample> geometry;
    std::uint64_t seed{};
    Revision parentRevision{};
    std::uint32_t engineVersion = 1;
    bool operator==(const StrokeEvent&) const = default;
};
using NodeMap = std::map<NodeId, LayerNode>;
using ResourceMap = std::map<Hash, Resource>;
using EventList = std::vector<StrokeEvent>;
struct DocumentSnapshot {
    DocumentId documentId;
    Revision revision{};
    DocumentSpec spec;
    std::shared_ptr<const NodeMap> nodes = std::make_shared<const NodeMap>();
    std::shared_ptr<const ResourceMap> resources = std::make_shared<const ResourceMap>();
    std::shared_ptr<const EventList> eventIndex = std::make_shared<const EventList>();
    bool sameContent(const DocumentSnapshot& b) const {
        return documentId == b.documentId && spec == b.spec && *nodes == *b.nodes &&
               *resources == *b.resources && *eventIndex == *b.eventIndex;
    }
};
struct ChangeSet {
    Revision before{}, after{};
    std::vector<Rect> dirtyRegions;
    std::vector<NodeId> changedNodes;
    std::vector<Hash> resourceRefs;
};
struct Command;
Result<void> validateSnapshot(const DocumentSnapshot&);
class Document {
    friend class JobBroker;
    Result<ChangeSet> commitExternal(const DocumentSnapshot&, Revision);
    DocumentSnapshot state_;
    std::vector<DocumentSnapshot> undo_, redo_;
    explicit Document(DocumentSnapshot s) : state_(std::move(s)) {}
    Result<ChangeSet> restore(std::vector<DocumentSnapshot>&, std::vector<DocumentSnapshot>&);

  public:
    static NodeId rootId() {
        return Id::fromNumber(1);
    }
    static Result<Document> create(const DocumentSpec&, DocumentId);
    static Result<Document> fromSnapshot(const DocumentSnapshot&);
    DocumentSnapshot snapshot() const {
        return state_;
    }
    Result<ChangeSet> apply(const Command&, Revision);
    Result<ChangeSet> undo();
    Result<ChangeSet> redo();
};
} // namespace drawborad
