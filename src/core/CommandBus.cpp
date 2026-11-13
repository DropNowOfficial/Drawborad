#include "Commands.h"
#include "view/LayerTree.h"
#include "EditableStroke.h"
#include "FillProvenance.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace drawborad {
namespace {
struct Failure {
    Error value;
};
void require(bool b, ErrorCode code, const char* key) {
    if (!b)
        throw Failure{error(code, key)};
}
std::vector<NodeId> subtree(const NodeMap& ns, NodeId id) {
    std::vector<NodeId> out, queue{id};
    while (!queue.empty()) {
        auto n = queue.back();
        queue.pop_back();
        out.push_back(n);
        for (auto c : ns.at(n).children)
            queue.push_back(c);
    }
    return out;
}
bool locked(const NodeMap& ns, NodeId id, bool skipSelf = false) {
    if (skipSelf)
        id = ns.at(id).parent;
    while (!id.empty()) {
        if (ns.at(id).locked)
            return true;
        id = ns.at(id).parent;
    }
    return false;
}
void writableTree(const NodeMap& ns, NodeId id) {
    for (auto x : subtree(ns, id))
        require(!locked(ns, x), ErrorCode::Locked, "node.locked");
}
void removeFromParent(NodeMap& ns, NodeId id) {
    auto& cs = ns.at(ns.at(id).parent).children;
    std::erase(cs, id);
}
void insert(NodeMap& ns, NodeId parent, NodeId id, std::size_t at) {
    auto& c = ns.at(parent).children;
    c.insert(c.begin() + std::min(at, c.size()), id);
    ns.at(id).parent = parent;
}
Rect region(const NodeMap& ns, NodeId id) {
    const auto b = ns.at(id).bounds;
    long double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int v = 0; v < 4; ++v) {
        long double x = static_cast<long double>(b.x) + ((v & 1) ? b.width : 0),
                    y = static_cast<long double>(b.y) + ((v & 2) ? b.height : 0);
        auto p = id;
        while (!p.empty()) {
            const auto& t = ns.at(p).transform.affine;
            auto xx = t[0] * x + t[2] * y + t[4];
            y = t[1] * x + t[3] * y + t[5];
            x = xx;
            p = ns.at(p).parent;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    require(
        std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1) &&
            x0 > static_cast<long double>(INT64_MIN) && y0 > static_cast<long double>(INT64_MIN) &&
            x1 < static_cast<long double>(INT64_MAX) && y1 < static_cast<long double>(INT64_MAX),
        ErrorCode::Overflow, "transform.range");
    auto w = std::ceil(x1) - std::floor(x0), h = std::ceil(y1) - std::floor(y0);
    require(w < 18446744073709551616.L && h < 18446744073709551616.L, ErrorCode::Overflow,
            "transform.bounds");
    return {std::int64_t(std::floor(x0)), std::int64_t(std::floor(y0)), std::uint64_t(w),
            std::uint64_t(h)};
}
} // namespace
Result<ChangeSet> Document::apply(const Command& cmd, Revision expected) {
    if (expected != state_.revision)
        return error(ErrorCode::StaleRevision, "command.revision");
    if (expected == UINT64_MAX)
        return error(ErrorCode::Overflow, "revision.overflow");
    if (cmd.version != 1 || cmd.operations.empty() || cmd.operations.size() > 65536)
        return error(ErrorCode::InvalidInput, "command.version-or-size");
    auto next = state_;
    auto ns = std::make_shared<NodeMap>(*state_.nodes);
    auto rs = std::make_shared<ResourceMap>(*state_.resources);
    std::shared_ptr<EventList> ev;
    next.nodes = ns;
    next.resources = rs;
    ChangeSet change;
    change.before = expected;
    change.after = expected + 1;
    std::set<NodeId> touched;
    auto mark = [&](NodeId id) {
        for (auto x : subtree(*ns, id)) {
            touched.insert(x);
            change.dirtyRegions.push_back(region(*ns, x));
        }
    };
    auto exists = [&](NodeId id) {
        require(ns->contains(id), ErrorCode::InvalidInput, "node.missing");
    };
    try {
        for (const auto& op : cmd.operations)
            std::visit(
                [&](const auto& o) {
                    using O = std::decay_t<decltype(o)>;
                    if constexpr (std::is_same_v<O, InstallResource>) {
                        require(!o.resource.id.empty() && o.resource.bytes != nullptr,
                                ErrorCode::InvalidInput, "resource.id");
                        auto r = o.resource;
                        r.bytes = std::make_shared<const Bytes>(*r.bytes);
                        auto it = rs->find(r.id);
                        require(it == rs->end() || it->second == r, ErrorCode::DigestMismatch,
                                "resource.id-collision");
                        (*rs)[r.id] = std::move(r);
                        change.resourceRefs.push_back(o.resource.id);
                    } else if constexpr (std::is_same_v<O, SetDependencies>) {
                        require(rs->contains(o.id), ErrorCode::InvalidInput, "resource.missing");
                        rs->at(o.id).dependencies = o.dependencies;
                        change.resourceRefs.push_back(o.id);
                    } else if constexpr (std::is_same_v<O, AddNode>) {
                        exists(o.node.parent);
                        require(!o.node.id.empty() && !ns->contains(o.node.id) &&
                                    o.node.children.empty(),
                                ErrorCode::InvalidInput, "node.duplicate-or-children");
                        require(!locked(*ns, o.node.parent), ErrorCode::Locked,
                                "node.parent-locked");
                        ns->emplace(o.node.id, o.node);
                        insert(*ns, o.node.parent, o.node.id, o.index);
                        mark(o.node.id);
                    } else if constexpr (std::is_same_v<O, RemoveNode>) {
                        exists(o.id);
                        require(o.id != rootId(), ErrorCode::InvalidInput, "node.remove-root");
                        writableTree(*ns, o.id);
                        mark(o.id);
                        auto list = subtree(*ns, o.id);
                        removeFromParent(*ns, o.id);
                        for (auto x : list)
                            ns->erase(x);
                    } else if constexpr (std::is_same_v<O, RenameNode>) {
                        exists(o.id);
                        require(!locked(*ns, o.id), ErrorCode::Locked, "node.locked");
                        ns->at(o.id).name = o.name;
                        touched.insert(o.id);
                    } else if constexpr (std::is_same_v<O, MoveNode>) {
                        exists(o.id);
                        exists(o.parent);
                        require(o.id != rootId(), ErrorCode::InvalidInput, "node.move-root");
                        auto p = o.parent;
                        while (!p.empty()) {
                            require(p != o.id, ErrorCode::Cycle, "node.cycle");
                            p = ns->at(p).parent;
                        }
                        writableTree(*ns, o.id);
                        require(!locked(*ns, o.parent), ErrorCode::Locked, "node.parent-locked");
                        mark(o.id);
                        removeFromParent(*ns, o.id);
                        insert(*ns, o.parent, o.id, o.index);
                        mark(o.id);
                    } else if constexpr (std::is_same_v<O, SetProperties>) {
                        exists(o.id);
                        bool unlockOnly = o.locked && !*o.locked && !o.visible && !o.opacity &&
                                          !o.blend && !o.transform;
                        require(!locked(*ns, o.id, unlockOnly), ErrorCode::Locked, "node.locked");
                        if (o.transform)
                            writableTree(*ns, o.id);
                        mark(o.id);
                        auto& n = ns->at(o.id);
                        if (o.visible)
                            n.visible = *o.visible;
                        if (o.locked)
                            n.locked = *o.locked;
                        if (o.opacity)
                            n.opacity = *o.opacity;
                        if (o.blend)
                            n.blend = *o.blend;
                        if (o.transform)
                            n.transform = *o.transform;
                    } else if constexpr (std::is_same_v<O, SetMask>) {
                        exists(o.id);
                        require(!locked(*ns, o.id), ErrorCode::Locked, "node.locked");
                        mark(o.id);
                        ns->at(o.id).maskId = o.mask;
                    } else if constexpr (std::is_same_v<O, DuplicateNode>) {
                        exists(o.id);
                        require(o.id != rootId(), ErrorCode::InvalidInput, "node.duplicate-root");
                        auto ids = subtree(*ns, o.id);
                        require(ids.size() == o.idMap.size(), ErrorCode::InvalidInput,
                                "duplicate.id-map");
                        std::set<Id> seen;
                        for (auto x : ids) {
                            require(o.idMap.contains(x), ErrorCode::InvalidInput,
                                    "duplicate.id-map");
                            auto z = o.idMap.at(x);
                            require(!z.empty() && !ns->contains(z) && seen.insert(z).second,
                                    ErrorCode::InvalidInput, "duplicate.id-collision");
                        }
                        const auto parent = ns->at(o.id).parent;
                        require(!locked(*ns, parent), ErrorCode::Locked, "duplicate.parent");
                        for (auto x : ids) {
                            auto n = ns->at(x);
                            n.id = o.idMap.at(x);
                            if (o.idMap.contains(n.parent))
                                n.parent = o.idMap.at(n.parent);
                            if (o.idMap.contains(n.objectId))
                                n.objectId = o.idMap.at(n.objectId);
                            for (auto& c : n.children)
                                c = o.idMap.at(c);
                            n.provenance.push_back(x);
                            ns->emplace(n.id, n);
                            touched.insert(n.id);
                        }
                        insert(*ns, parent, o.idMap.at(o.id), SIZE_MAX);
                        mark(o.idMap.at(o.id));
                    } else if constexpr (std::is_same_v<O, MergeNodes>) {
                        require(o.ids.size() >= 2 && !o.groupId.empty() && !ns->contains(o.groupId),
                                ErrorCode::InvalidInput, "merge.input");
                        exists(o.ids[0]);
                        auto parent = ns->at(o.ids[0]).parent;
                        exists(parent);
                        auto before = ns->at(parent).children;
                        std::set<Id> selected(o.ids.begin(), o.ids.end());
                        require(selected.size() == o.ids.size(), ErrorCode::InvalidInput,
                                "merge.duplicate");
                        LayerNode n;
                        n.id = o.groupId;
                        n.parent = parent;
                        n.kind = NodeKind::Group;
                        n.name = o.name;
                        n.composite = true;
                        n.provenance = o.ids;
                        n.objectId = ns->at(o.ids[0]).objectId;
                        std::size_t pos = before.size();
                        for (std::size_t i = 0; i < before.size(); ++i)
                            if (selected.contains(before[i]))
                                pos = std::min(pos, i);
                        for (auto id : o.ids) {
                            exists(id);
                            require(ns->at(id).parent == parent, ErrorCode::InvalidInput,
                                    "merge.siblings");
                            writableTree(*ns, id);
                            mark(id);
                        }
                        ns->emplace(n.id, n);
                        for (auto id : before)
                            if (selected.contains(id)) {
                                removeFromParent(*ns, id);
                                insert(*ns, n.id, id, SIZE_MAX);
                            }
                        insert(*ns, parent, n.id, pos);
                        touched.insert(n.id);
                    } else if constexpr (std::is_same_v<O, SplitNode>) {
                        exists(o.groupId);
                        auto n = ns->at(o.groupId);
                        require(n.composite && n.kind == NodeKind::Group, ErrorCode::InvalidInput,
                                "split.not-composite");
                        require(n.visible && n.opacity == 1 && n.blend == BlendMode::Normal &&
                                    n.transform == LayerTransform{} && n.maskId.empty() &&
                                    n.content.empty(),
                                ErrorCode::UnsupportedFeature,
                                "split.visual-properties-require-baking");
                        writableTree(*ns, n.id);
                        mark(n.id);
                        auto& siblings = ns->at(n.parent).children;
                        auto at = std::size_t(std::find(siblings.begin(), siblings.end(), n.id) -
                                              siblings.begin());
                        removeFromParent(*ns, n.id);
                        for (auto id : n.children)
                            insert(*ns, n.parent, id, at++);
                        ns->erase(n.id);
                    } else if constexpr (std::is_same_v<O, RecolorStroke>) {
                        if (!ev)
                            ev = std::make_shared<EventList>(*next.eventIndex);
                        auto found = std::find_if(ev->begin(), ev->end(),
                            [&](const auto& event) { return event.id == o.id; });
                        require(found != ev->end(), ErrorCode::InvalidInput, "stroke.missing");
                        exists(found->layerId);
                        require(!locked(*ns, found->layerId), ErrorCode::Locked, "stroke.locked");
                        found->color = o.color;
                        next.eventIndex = ev;
                        mark(found->layerId);
                    } else if constexpr (std::is_same_v<O, AdjustStrokeSamples>) {
                        require(cmd.operations.size()==1,ErrorCode::InvalidInput,"stroke-edit.single-operation");
                        const auto eligible=editableStrokeIndex(next,o.id);
                        if(!eligible)throw Failure{eligible.error()};
                        const auto index=eligible.value();const auto& old=(*next.eventIndex)[index];
                        require(!o.geometry.empty()&&o.geometry.size()==old.geometry.size(),ErrorCode::InvalidInput,"stroke-edit.smooth-shape");
                        for(std::size_t i=0;i<o.geometry.size();++i){
                            const auto& p=o.geometry[i];const auto& before=old.geometry[i];
                            require(p.timestampUs==before.timestampUs && p.tiltX==before.tiltX && p.tiltY==before.tiltY && p.deviceKind==before.deviceKind && p.capabilities==before.capabilities,
                                    ErrorCode::InvalidInput,"stroke-edit.sample-metadata");
                            require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.pressure)&&p.pressure>=0&&p.pressure<=1&&p.x>=0&&p.y>=0&&p.x<=double(next.spec.pixelSize.width)&&p.y<=double(next.spec.pixelSize.height),
                                    ErrorCode::InvalidInput,"stroke-edit.sample-bounds");
                        }
                        ev=std::make_shared<EventList>(*next.eventIndex);
                        (*ev)[index].geometry=o.geometry;next.eventIndex=ev;
                        mark(old.layerId);
                        // Cover both old and new footprint, even when a legacy
                        // layer's declared bounds do not enclose its strokes.
                        change.dirtyRegions.push_back({0,0,next.spec.pixelSize.width,next.spec.pixelSize.height});
                    } else if constexpr (std::is_same_v<O, RecordClosedInkFill>) {
                        require(!cmd.operations.empty() && std::holds_alternative<RecordClosedInkFill>(cmd.operations.back()),ErrorCode::InvalidInput,"fill-source.record-last");
                        require(std::count_if(cmd.operations.begin(),cmd.operations.end(),[](const auto&x){return std::holds_alternative<RecordClosedInkFill>(x);})==1,ErrorCode::InvalidInput,"fill-source.one-record");
                        auto boundary=std::find_if(state_.eventIndex->begin(),state_.eventIndex->end(),[&](const auto&e){return e.id==o.operation.boundaryEvent;});
                        require(boundary!=state_.eventIndex->end() && boundary->layerId==o.operation.boundaryLayer && boundary->engineVersion==19 && fill_provenance::eventDigest(*boundary)==o.operation.boundaryDigest,ErrorCode::InvalidInput,"fill-source.preexisting-boundary");
                        require(state_.nodes->contains(o.operation.source)&&state_.nodes->contains(o.operation.target)&&state_.nodes->contains(o.operation.boundaryLayer),ErrorCode::InvalidInput,"fill-source.preexisting-nodes");
                        require(state_.nodes->at(o.operation.source).sourceResource==o.operation.sourceResource && state_.resources->contains(o.operation.sourceResource),ErrorCode::InvalidInput,"fill-source.creation-resource");
                        require(sha256(*state_.resources->at(o.operation.sourceResource).bytes)==o.operation.sourceDigest,ErrorCode::InvalidInput,"fill-source.creation-resource");
                        require(fill_provenance::contextDigest(state_,o.operation)==o.operation.contextDigest,ErrorCode::InvalidInput,"fill-source.creation-context");
                        require(next.fillOperations.size()<256,ErrorCode::BudgetExceeded,"fill-source.record-budget");
                        require(cmd.operations.size()==o.operation.members.size()+1,ErrorCode::InvalidInput,"fill-source.atomic-members");
                        std::set<Id> painted;
                        for(const auto& member:cmd.operations)if(const auto*p=std::get_if<Paint>(&member))painted.insert(p->event.id);else require(std::holds_alternative<RecordClosedInkFill>(member),ErrorCode::InvalidInput,"fill-source.atomic-members");
                        require(painted.size()==o.operation.members.size(),ErrorCode::InvalidInput,"fill-source.atomic-members");
                        for(const auto&[id,digest]:o.operation.members){(void)digest;require(painted.contains(id),ErrorCode::InvalidInput,"fill-source.atomic-members");}
                        next.fillOperations.push_back(o.operation);
                    } else if constexpr (std::is_same_v<O, Paint>) {
                        const auto& e = o.event;
                        exists(e.layerId);
                        require(ns->at(e.layerId).kind == NodeKind::Layer &&
                                    ns->at(e.layerId).objectId == e.objectId,
                                ErrorCode::InvalidInput, "paint.layer");
                        require(!locked(*ns, e.layerId), ErrorCode::Locked, "paint.locked");
                        require(e.parentRevision == expected, ErrorCode::StaleRevision,
                                "paint.revision");
                        if (!ev)
                            ev = std::make_shared<EventList>(*state_.eventIndex);
                        ev->push_back(e);
                        next.eventIndex = ev;
                        change.dirtyRegions.push_back(o.dirty);
                        touched.insert(e.layerId);
                    } else if constexpr (std::is_same_v<O, ConvertColor>) {
                        for (auto& [id, n] : *ns) {
                            require(!locked(*ns, id), ErrorCode::Locked, "convert.locked");
                            if (!n.content.empty())
                                require(o.replacementContent.contains(id), ErrorCode::InvalidInput,
                                        "convert.incomplete");
                        }
                        for (auto& [id, refs] : o.replacementContent) {
                            exists(id);
                            ns->at(id).content = refs;
                        }
                        require(o.events != nullptr, ErrorCode::InvalidInput, "convert.events");
                        next.eventIndex = std::make_shared<const EventList>(*o.events);
                        next.spec.colorSpec = o.target;
                        require(next.spec.background.transparent || o.paperColor.has_value(),
                                ErrorCode::ConversionConsentRequired,
                                "convert.paper-must-be-explicit");
                        if (o.paperColor)
                            next.spec.background.paperColor = *o.paperColor;
                        auto count =
                            checkedMultiply(next.spec.pixelSize.width, next.spec.pixelSize.height);
                        require(bool(count), ErrorCode::Overflow, "convert.pixels");
                        auto bytes = checkedMultiply(
                            count.value(), std::uint64_t(o.target.format.channelCount()) *
                                               (o.target.format.depth == Depth::U16 ? 2 : 1));
                        require(bool(bytes), ErrorCode::Overflow, "convert.bytes");
                        next.spec.estimatedBytes = bytes.value();
                        mark(rootId());
                    }
                },
                op);
        next.revision = expected + 1;
        auto v = validateSnapshot(next);
        if (!v)
            return v.error();
        for (auto id : touched)
            if (ns->contains(id))
                change.dirtyRegions.push_back(region(*ns, id));
    } catch (const Failure& f) {
        return f.value;
    }
    change.changedNodes.assign(touched.begin(), touched.end());
    undo_.push_back(state_);
    redo_.clear();
    state_ = std::move(next);
    return change;
}
} // namespace drawborad
