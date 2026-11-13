#include "core/HiddenBoundary.h"
#include "core/CroppedStrokeMask.h"
#include "Commands.h"
#include "MyPaintCroppedRaster.h"
#include "BucketEvent.h"
#include "FreehandEvent.h"
#include "DryInkEvent.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace drawborad {
namespace {
bool finiteTransform(const LayerTransform& t) {
    for (auto x : t.affine)
        if (!std::isfinite(x))
            return false;
    auto& a = t.affine;
    double det = a[0] * a[3] - a[1] * a[2];
    return std::isfinite(det) && std::abs(det) > 1e-12;
}
bool validColor(const ColorValue& c, ColorMode m) {
    if (c.activeChannels != (m == ColorMode::RGB ? 3 : 4) || !std::isfinite(c.alpha) ||
        c.alpha < 0 || c.alpha > 1)
        return false;
    for (auto x : c.channels)
        if (!std::isfinite(x) || x < 0 || x > 1)
            return false;
    return true;
}
} // namespace
Result<void> validateSnapshot(const DocumentSnapshot& s) {
    if (s.documentId.empty() || !s.nodes || !s.resources || !s.eventIndex ||
        s.nodes->size() > 65536)
        return error(ErrorCode::CorruptData, "document.structure");
    auto descriptor = validateDocumentSpec(s.spec);
    if (!descriptor)
        return descriptor.error();
    const auto& ns = *s.nodes;
    auto root = ns.find(Document::rootId());
    if (root == ns.end() || root->second.kind != NodeKind::Root || !root->second.parent.empty())
        return error(ErrorCode::CorruptData, "document.root");
    auto f = s.spec.colorSpec.format;
    if (!s.spec.pixelSize.width || !s.spec.pixelSize.height || !std::isfinite(s.spec.ppi) ||
        s.spec.ppi <= 0 || (f.mode != ColorMode::RGB && f.mode != ColorMode::CMYK) ||
        (f.depth != Depth::U8 && f.depth != Depth::U16))
        return error(ErrorCode::CorruptData, "document.spec");
    if (f.mode == ColorMode::CMYK && s.spec.colorSpec.profileId.empty())
        return error(ErrorCode::ProfileRequired, "color.cmyk-profile");
    for(const auto& [hash,r]:*s.resources)if(cropMask::tagged(r)){auto mask=cropMask::parse(s,r);if(!mask)return mask.error();}
    std::set<NodeId> listed;
    for (auto& [id, n] : ns) {
        if (id.empty() || n.id != id || n.name.size() > 16384 || n.stage.size() > 256 ||
            !finiteTransform(n.transform) || !std::isfinite(n.opacity) || n.opacity < 0 ||
            n.opacity > 1 || int(n.kind) < 0 || int(n.kind) > 3 || int(n.sourceKind) < 0 ||
            int(n.sourceKind) > 2 || int(n.blend) < 0 || int(n.blend) > 4)
            return error(ErrorCode::InvalidInput, "node.properties");
        if (n.kind == NodeKind::Layer && !n.children.empty())
            return error(ErrorCode::InvalidInput, "node.layer-children");
        if (n.kind == NodeKind::Root && id != Document::rootId())
            return error(ErrorCode::InvalidInput, "node.second-root");
        if (id != Document::rootId() &&
            (!ns.contains(n.parent) || ns.at(n.parent).kind == NodeKind::Layer))
            return error(ErrorCode::InvalidInput, "node.parent");
        if (!n.objectId.empty() &&
            (!ns.contains(n.objectId) || ns.at(n.objectId).kind != NodeKind::Object))
            return error(ErrorCode::InvalidInput, "node.object");
        if (n.sourceKind == SourceKind::Reconstructed && !n.sourceResource.empty())
            return error(ErrorCode::InvalidInput, "node.hidden-source");
        if(n.stage!=hiddenBoundary::Stage){for(const auto hash:n.content)if(s.resources->contains(hash)&&hiddenBoundary::tagged(s.resources->at(hash)))return error(ErrorCode::UnsupportedFeature,"boundary.hidden-resource");if(s.resources->contains(n.sourceResource)&&hiddenBoundary::tagged(s.resources->at(n.sourceResource)))return error(ErrorCode::UnsupportedFeature,"boundary.hidden-resource");}
        for (auto h : n.content)
            if (!s.resources->contains(h))
                return error(ErrorCode::CorruptData, "node.content-missing");
        if (!n.maskId.empty() && (!s.resources->contains(n.maskId) ||
                                  s.resources->at(n.maskId).kind != ResourceKind::Mask))
            return error(ErrorCode::CorruptData, "node.mask-missing");
        if(!n.maskId.empty() && cropMask::tagged(s.resources->at(n.maskId)) && !croppedRaster::isStage(n.stage))return error(ErrorCode::UnsupportedFeature,"stroke-mask.target");
        if (!n.sourceResource.empty() && !s.resources->contains(n.sourceResource))
            return error(ErrorCode::CorruptData, "node.source-missing");
        for (auto c : n.children) {
            if (!ns.contains(c) || ns.at(c).parent != id || !listed.insert(c).second)
                return error(ErrorCode::Cycle, "node.ownership");
        }
        std::set<NodeId> chain;
        auto p = id;
        while (!p.empty()) {
            if (!chain.insert(p).second)
                return error(ErrorCode::Cycle, "node.cycle");
            if (!ns.contains(p))
                return error(ErrorCode::CorruptData, "node.parent");
            if (chain.size() > 512)
                return error(ErrorCode::BudgetExceeded, "node.depth");
            p = ns.at(p).parent;
        }
    }
    if (listed.size() + 1 != ns.size())
        return error(ErrorCode::CorruptData, "node.unreachable");
    std::map<Hash, int> colors;
    std::uint64_t resourceBytes=0;bool hasLargeResource=false;
    for (auto& [id, r] : *s.resources) {
        if (id.empty() || r.id != id || !r.bytes || r.bytes->size() > MaxResourceBytes ||
            int(r.kind) < 0 || int(r.kind) > 4)
            return error(ErrorCode::CorruptData, "resource.invalid");
        hasLargeResource=hasLargeResource || r.bytes->size()>LegacyMaxResourceBytes;
        auto total=checkedAdd(resourceBytes,r.bytes->size());if(!total)return total.error();resourceBytes=total.value();
        if(hasLargeResource && resourceBytes>MaxLargeDocumentResourceBytes)return error(ErrorCode::BudgetExceeded,"resource.large-document-budget");
        std::vector<std::pair<Hash, bool>> stack{{id, false}};
        while (!stack.empty()) {
            auto [h, exit] = stack.back();
            stack.pop_back();
            if (exit) {
                colors[h] = 2;
                continue;
            }
            if (colors[h] == 2)
                continue;
            if (colors[h] == 1)
                return error(ErrorCode::Cycle, "resource.cycle");
            auto it = s.resources->find(h);
            if (it == s.resources->end())
                return error(ErrorCode::CorruptData, "resource.missing");
            colors[h] = 1;
            stack.push_back({h, true});
            for (auto d : it->second.dependencies)
                stack.push_back({d, false});
        }
    }
    std::set<Id> events;
    for (auto& e : *s.eventIndex) {
        if(e.toolId=="smooth-ink-aa" && e.engineVersion!=19)return error(ErrorCode::UnsupportedFeature,"smooth-ink.engine-tool");
        if(e.toolId=="dry-ink" && e.engineVersion!=12)return error(ErrorCode::UnsupportedFeature,"dry-ink.engine-tool");
        if(e.toolId=="fineliner-aa" && e.engineVersion!=10)return error(ErrorCode::UnsupportedFeature,"freehand.engine-tool");
        if (e.engineVersion!=1 && e.engineVersion!=2 && e.engineVersion!=8 && e.engineVersion!=10 && e.engineVersion!=12 && e.engineVersion!=19)
            return error(ErrorCode::UnsupportedFeature,"event.engine-version");
        if (e.engineVersion==12) {auto valid=validateDryInkEvent(e);if(!valid)return valid.error();}
        if (e.engineVersion==19) {auto valid=validateSmoothInkEvent(e);if(!valid)return valid.error();}
        if (e.engineVersion==10) {auto valid=validateFreehandEvent(e);if(!valid)return valid.error();}
        if (e.engineVersion==8) {
            auto valid=validateBucketEvent(e); if (!valid) return valid.error();
            for (std::size_t i=0;i<e.geometry.size();i+=2)
                if (e.geometry[i+1].x>s.spec.pixelSize.width ||
                    e.geometry[i].y>=s.spec.pixelSize.height)
                    return error(ErrorCode::InvalidInput,"bucket.canvas");
        }
        if (e.id.empty() || !events.insert(e.id).second || e.geometry.empty() ||
            e.geometry.size() > 1000000 || e.startUs > e.endUs || !validColor(e.color, f.mode) ||
            e.color.profileId != s.spec.colorSpec.profileId ||
            e.toolId.empty() ||
            e.toolId.size() > 256 || !std::isfinite(e.style.width) || e.style.width <= 0 ||
            e.style.width > 8192 || !std::isfinite(e.style.opacity) || e.style.opacity < 0 ||
            e.style.opacity > 1 || !std::isfinite(e.style.hardness) || e.style.hardness < 0 ||
            e.style.hardness > 1 || e.style.angle > 7 ||
            (e.style.shape != NibShape::Round && e.style.shape != NibShape::Chisel) ||
            (!e.style.grain.empty() && e.style.grain.size() != 64) ||
            (e.engineVersion == 1 && e.style != StrokeStyle{}))
            return error(ErrorCode::CorruptData, "event.invalid");
        std::uint64_t last = 0;
        for (auto& p : e.geometry) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.pressure) ||
                p.pressure < 0 || p.pressure > 1 || p.timestampUs < last ||
                (p.tiltX && (!std::isfinite(*p.tiltX) || std::abs(*p.tiltX) > 90)) ||
                (p.tiltY && (!std::isfinite(*p.tiltY) || std::abs(*p.tiltY) > 90)))
                return error(ErrorCode::InvalidInput, "event.sample");
            last = p.timestampUs;
        }
    }
    std::size_t boundaryCount=0,boundaryPoints=0;
    for(const auto&[id,node]:ns)if(node.stage==hiddenBoundary::Stage){auto v=hiddenBoundary::parse(s,node);if(!v)return v.error();boundaryPoints+=v.value().points.size();if(++boundaryCount>32||boundaryPoints>4096)return error(ErrorCode::BudgetExceeded,"boundary.document-limit");}
    for(const auto&e:*s.eventIndex)if(ns.contains(e.layerId)&&ns.at(e.layerId).stage==hiddenBoundary::Stage)return error(ErrorCode::UnsupportedFeature,"boundary.paint-event");
    bool hasCropped=false;
    for(const auto& [id,node]:ns)if(croppedRaster::isStage(node.stage)){
        hasCropped=true;auto parsed=croppedRaster::parse(s,id);if(!parsed)return parsed.error();
    }
    if(hasCropped && resourceBytes>MaxLargeDocumentResourceBytes)
        return error(ErrorCode::BudgetExceeded,"crop.document-resources");
    for(const auto& e:*s.eventIndex)if(ns.contains(e.layerId) && croppedRaster::isStage(ns.at(e.layerId).stage))
        return error(ErrorCode::UnsupportedFeature,"crop.paint-events");
    return {};
}
Result<Document> Document::create(const DocumentSpec& s, DocumentId id) {
    DocumentSnapshot snap;
    snap.documentId = id;
    snap.spec = s;
    auto nodes = std::make_shared<NodeMap>();
    LayerNode root;
    root.id = rootId();
    root.kind = NodeKind::Root;
    root.name = "Document";
    nodes->emplace(root.id, root);
    snap.nodes = nodes;
    return fromSnapshot(snap);
}
Result<Document> Document::fromSnapshot(const DocumentSnapshot& s) {
    auto ok = validateSnapshot(s);
    if (!ok)
        return ok.error();
    auto owned = s;
    owned.nodes = std::make_shared<const NodeMap>(*s.nodes);
    auto resources = std::make_shared<ResourceMap>(*s.resources);
    for (auto& [id, r] : *resources) {
        (void)id;
        r.bytes = std::make_shared<const Bytes>(*r.bytes);
    }
    owned.resources = resources;
    owned.eventIndex = std::make_shared<const EventList>(*s.eventIndex);
    return Document(std::move(owned));
}
Result<ChangeSet> Document::restore(std::vector<DocumentSnapshot>& from,
                                    std::vector<DocumentSnapshot>& to) {
    if (from.empty())
        return error(ErrorCode::InvalidInput, "history.empty");
    if (state_.revision == UINT64_MAX)
        return error(ErrorCode::Overflow, "revision.overflow");
    auto next = from.back();
    ChangeSet c;
    c.before = state_.revision;
    c.after = c.before + 1;
    c.dirtyRegions.push_back({0, 0, state_.spec.pixelSize.width, state_.spec.pixelSize.height});
    for (auto& [id, n] : *next.nodes) {
        (void)n;
        c.changedNodes.push_back(id);
    }
    for (const auto& [id, node] : *state_.nodes) {
        (void)node;
        if (!next.nodes->contains(id))
            c.changedNodes.push_back(id);
    }
    to.push_back(state_);
    from.pop_back();
    next.revision = c.after;
    state_ = std::move(next);
    return c;
}
Result<ChangeSet> Document::undo() { return restore(undo_, redo_); }
Result<ChangeSet> Document::redo() { return restore(redo_, undo_); }
Result<ChangeSet> Document::commitExternal(const DocumentSnapshot& next, Revision expected) {
    if (state_.revision != expected || expected == UINT64_MAX || next.revision != expected + 1 ||
        next.documentId != state_.documentId)
        return error(ErrorCode::StaleRevision, "worker.revision");
    if (next.spec != state_.spec)
        return error(ErrorCode::InvalidInput, "worker.cannot-change-document-spec");
    auto valid = validateSnapshot(next);
    if (!valid)
        return valid.error();
    if (next.eventIndex->size() < state_.eventIndex->size() ||
        !std::equal(state_.eventIndex->begin(), state_.eventIndex->end(), next.eventIndex->begin()))
        return error(ErrorCode::InvalidInput, "worker.event-history-rewrite");
    for (std::size_t i = state_.eventIndex->size(); i < next.eventIndex->size(); ++i) {
        const auto& e = next.eventIndex->at(i);
        auto n = next.nodes->find(e.layerId);
        if (n == next.nodes->end() || n->second.kind != NodeKind::Layer ||
            n->second.objectId != e.objectId || e.parentRevision != expected)
            return error(ErrorCode::InvalidInput, "worker.event-owner");
        for (auto id = e.layerId; !id.empty(); id = next.nodes->at(id).parent)
            if (next.nodes->at(id).locked)
                return error(ErrorCode::Locked, "worker.event-locked");
    }
    for (auto& [id, n] : *state_.nodes) {
        bool isLocked = false;
        auto p = id;
        while (!p.empty()) {
            if (state_.nodes->at(p).locked)
                isLocked = true;
            p = state_.nodes->at(p).parent;
        }
        if (isLocked && (!next.nodes->contains(id) || next.nodes->at(id) != n))
            return error(ErrorCode::Locked, "worker.locked");
    }
    for (auto& [id, r] : *state_.resources) {
        auto it = next.resources->find(id);
        if (it != next.resources->end() && it->second != r)
            return error(ErrorCode::DigestMismatch, "worker.resource-rewrite");
    }
    ChangeSet c;
    c.before = expected;
    c.after = expected + 1;
    for (auto& [id, n] : *next.nodes) {
        auto old = state_.nodes->find(id);
        if (old == state_.nodes->end() || old->second != n) {
            c.changedNodes.push_back(id);
            c.dirtyRegions.push_back(n.bounds);
        }
    }
    for (auto& [id, n] : *state_.nodes)
        if (!next.nodes->contains(id)) {
            c.changedNodes.push_back(id);
            c.dirtyRegions.push_back(n.bounds);
        }
    for (auto& [id, r] : *next.resources) {
        (void)r;
        if (!state_.resources->contains(id))
            c.resourceRefs.push_back(id);
    }
    for (std::size_t i = state_.eventIndex->size(); i < next.eventIndex->size(); ++i) {
        const auto id = next.eventIndex->at(i).layerId;
        if (std::find(c.changedNodes.begin(), c.changedNodes.end(), id) == c.changedNodes.end())
            c.changedNodes.push_back(id);
    }
    if (!c.changedNodes.empty())
        c.dirtyRegions.push_back({0, 0, state_.spec.pixelSize.width, state_.spec.pixelSize.height});
    undo_.push_back(state_);
    redo_.clear();
    state_ = next;
    return c;
}
} // namespace drawborad
