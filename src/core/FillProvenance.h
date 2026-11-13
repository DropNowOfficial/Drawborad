#pragma once
#include "Document.h"
#include "Digest.h"
#include "storage/Binary.h"
#include <set>
namespace drawborad {
namespace fill_provenance {
inline void color(binary::Writer& w,const ColorValue& c){w.hash(c.profileId);for(auto x:c.channels)w.real(x);w.u64(c.activeChannels);w.real(c.alpha);}
inline Hash eventDigest(const StrokeEvent& e){binary::Writer w;w.text("fill-event-v1");w.id(e.id);w.id(e.objectId);w.id(e.layerId);w.u64(e.startUs);w.u64(e.endUs);w.text(e.toolId);color(w,e.color);w.u64(e.geometry.size());for(const auto&p:e.geometry){w.real(p.x);w.real(p.y);w.real(p.pressure);w.boolean(bool(p.tiltX));if(p.tiltX)w.real(*p.tiltX);w.boolean(bool(p.tiltY));if(p.tiltY)w.real(*p.tiltY);w.u64(p.timestampUs);w.u64(p.deviceKind);w.u64(p.capabilities);}w.u64(e.seed);w.u64(e.parentRevision);w.u64(e.engineVersion);w.real(e.style.width);w.real(e.style.opacity);w.real(e.style.hardness);w.u64(std::uint64_t(e.style.shape));w.u64(e.style.angle);w.blob(e.style.grain);return sha256(w.data);}
inline void nodeContext(binary::Writer&w,const LayerNode&n){w.id(n.id);w.id(n.parent);w.id(n.objectId);w.u64(std::uint64_t(n.kind));w.text(n.stage);w.hash(n.sourceResource);w.hash(n.maskId);w.u64(n.content.size());for(auto x:n.content)w.hash(x);for(auto x:n.transform.affine)w.real(x);w.real(n.opacity);w.u64(std::uint64_t(n.blend));w.boolean(n.visible);w.boolean(n.locked);w.u64(std::uint64_t(n.sourceKind));w.u64(n.bounds.x);w.u64(n.bounds.y);w.u64(n.bounds.width);w.u64(n.bounds.height);w.boolean(n.composite);w.u64(n.children.size());for(auto x:n.children)w.id(x);w.u64(n.provenance.size());for(auto x:n.provenance)w.id(x);}
inline Hash contextDigest(const DocumentSnapshot&s,const FillOperation&o){binary::Writer w;w.text("closed-ink-context-v1");w.u64(s.spec.pixelSize.width);w.u64(s.spec.pixelSize.height);w.u64(std::uint64_t(s.spec.colorSpec.format.mode));w.u64(std::uint64_t(s.spec.colorSpec.format.depth));w.boolean(s.spec.colorSpec.format.alpha);w.hash(s.spec.colorSpec.profileId);for(auto id:{o.target,o.source,o.boundaryLayer})nodeContext(w,s.nodes->at(id));const auto&resource=s.resources->at(s.nodes->at(o.source).sourceResource);w.u64(std::uint64_t(resource.kind));w.u64(resource.dependencies.size());for(auto id:resource.dependencies){w.hash(id);const auto it=s.resources->find(id);if(it!=s.resources->end()){w.hash(sha256(*it->second.bytes));w.u64(std::uint64_t(it->second.kind));}}const auto&root=s.nodes->at(Document::rootId());for(auto id:root.children)if(id==o.target||id==o.source||id==o.boundaryLayer)w.id(id);w.real(root.opacity);w.u64(std::uint64_t(root.blend));w.boolean(root.visible);w.boolean(root.locked);w.hash(root.maskId);for(auto x:root.transform.affine)w.real(x);return sha256(w.data);}
inline Result<void> validate(const DocumentSnapshot&s){
 if(s.fillOperations.empty())return {};
 if(s.fillOperations.size()>256)return error(ErrorCode::BudgetExceeded,"fill-source.record-budget");
 std::map<Hash,Hash> resourceHashes;std::map<Id,Hash> eventHashes;std::uint64_t hashedPoints=0;
 auto eventHash=[&](const StrokeEvent&e)->Hash{auto it=eventHashes.find(e.id);if(it!=eventHashes.end())return it->second;auto h=eventDigest(e);eventHashes.emplace(e.id,h);return h;};
 auto resourceHash=[&](Hash id)->Hash{auto it=resourceHashes.find(id);if(it!=resourceHashes.end())return it->second;auto h=sha256(*s.resources->at(id).bytes);resourceHashes.emplace(id,h);return h;};
 std::set<Id> groups,owned;std::map<Id,const StrokeEvent*> events;for(const auto&e:*s.eventIndex)events.emplace(e.id,&e);
 for(const auto&o:s.fillOperations){
 if(o.recipeVersion!=1)return error(ErrorCode::UnsupportedFeature,"fill-source.recipe-version");
 if(o.id.empty()||!groups.insert(o.id).second||o.members.empty()||o.members.size()>4||o.id!=o.members.front().first)return error(ErrorCode::CorruptData,"fill-source.members");
 for(auto id:{o.target,o.source,o.boundaryLayer})if(!s.nodes->contains(id))return error(ErrorCode::CorruptData,"fill-source.missing-node");
 if(o.target==o.source||o.target==o.boundaryLayer||o.source==o.boundaryLayer||!events.contains(o.boundaryEvent))return error(ErrorCode::CorruptData,"fill-source.boundary");
 const auto&boundary=*events.at(o.boundaryEvent);if(!eventHashes.contains(boundary.id)){if(boundary.geometry.size()>1000000-hashedPoints)return error(ErrorCode::BudgetExceeded,"fill-source.point-budget");hashedPoints+=boundary.geometry.size();}if(boundary.layerId!=o.boundaryLayer||boundary.engineVersion!=19||boundary.toolId!="smooth-ink-aa"||eventHash(boundary)!=o.boundaryDigest)return error(ErrorCode::UnsupportedFeature,"fill-source.boundary-changed");
 if(s.nodes->at(o.source).sourceResource!=o.sourceResource||!s.resources->contains(o.sourceResource)||resourceHash(o.sourceResource)!=o.sourceDigest)return error(ErrorCode::UnsupportedFeature,"fill-source.context-changed");
 for(const auto&[id,digest]:o.members){if(!owned.insert(id).second||!events.contains(id))return error(ErrorCode::CorruptData,"fill-source.member-missing-or-owned");const auto&e=*events.at(id);if(e.geometry.size()>1000000-hashedPoints)return error(ErrorCode::BudgetExceeded,"fill-source.point-budget");hashedPoints+=e.geometry.size();if(e.layerId!=o.target||e.engineVersion!=8||e.toolId!="bucket"||eventHash(e)!=digest)return error(ErrorCode::UnsupportedFeature,"fill-source.member-changed");}
 }
 return {};
}
}
}
