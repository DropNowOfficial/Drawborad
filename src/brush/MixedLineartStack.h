#pragma once
#include "HollowDerivedReference.h"
#include "render/CoverageRasterizer.h"
#include <set>
namespace drawborad {
inline constexpr const char* FinishingLayerStage="drawborad.finishing.v1";
struct MixedLineartView { DocumentSnapshot controlled; std::vector<NodeId> finishing; };
inline Result<MixedLineartView> projectControlledLineart(const DocumentSnapshot& s,std::stop_token stop={}) try {
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"mixed.cancelled");
 const auto w=s.spec.pixelSize.width,h=s.spec.pixelSize.height;
 if(!w||!h||w>8192||h>8192||w*h>20000000)return error(ErrorCode::BudgetExceeded,"mixed.canvas-budget");
 if(s.spec.colorSpec.format!=PixelFormat{ColorMode::RGB,Depth::U8,true})return error(ErrorCode::UnsupportedFeature,"mixed.rgba8-required");
 if(!s.nodes||!s.eventIndex||!s.resources||s.nodes->size()>4096||s.eventIndex->size()>8192)return error(ErrorCode::BudgetExceeded,"mixed.budget");
 std::uint64_t points=0;for(const auto& e:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"mixed.cancelled");if(e.geometry.size()>1000000-points)return error(ErrorCode::BudgetExceeded,"mixed.points");points+=e.geometry.size();}
 for(const auto& [id,n]:*s.nodes){(void)id;if(n.composite||!n.provenance.empty())return error(ErrorCode::UnsupportedFeature,"mixed.composite-provenance");}
 auto valid=validateSnapshot(s);if(!valid)return valid.error();
 const auto& root=s.nodes->at(Document::rootId());std::vector<NodeId> finishing;bool suffix=false;
 for(auto id:root.children){const auto& n=s.nodes->at(id);if(n.stage==FinishingLayerStage){suffix=true;finishing.push_back(id);if(finishing.size()>16)return error(ErrorCode::BudgetExceeded,"mixed.layers");
 if(n.kind!=NodeKind::Layer||n.parent!=Document::rootId()||!n.objectId.empty()||!n.children.empty()||!n.content.empty()||!n.sourceResource.empty()||n.sourceKind!=SourceKind::Reconstructed||!n.maskId.empty()||n.transform!=LayerTransform{}||n.opacity!=1||n.blend!=BlendMode::Normal||n.composite||!n.provenance.empty()||n.bounds!=Rect{0,0,s.spec.pixelSize.width,s.spec.pixelSize.height})return error(ErrorCode::UnsupportedFeature,"mixed.finishing-properties");
 }else if(suffix)return error(ErrorCode::UnsupportedFeature,"mixed.non-suffix");}
 auto nodes=std::make_shared<NodeMap>(*s.nodes);auto events=std::make_shared<EventList>();
 std::set<NodeId> ids(finishing.begin(),finishing.end());
 for(const auto& e:*s.eventIndex){if(stop.stop_requested())return error(ErrorCode::Cancelled,"mixed.cancelled");if(!ids.contains(e.layerId)){events->push_back(e);continue;}
 if(e.engineVersion!=1&&e.engineVersion!=2&&e.engineVersion!=10&&e.engineVersion!=12&&e.engineVersion!=19)return error(ErrorCode::UnsupportedFeature,"mixed.finishing-engine");
 auto checked=validateRasterEvent(e);if(!checked)return checked.error();}
 for(auto id:finishing){nodes->erase(id);}
 auto& children=nodes->at(Document::rootId()).children;children.resize(children.size()-finishing.size());
 DocumentSnapshot controlled=s;controlled.nodes=nodes;controlled.eventIndex=events;
 if(children.size()<2||children.size()>4||nodes->size()!=children.size()+1)return error(ErrorCode::UnsupportedFeature,"mixed.controlled-stack");
 if(children.size()>=3){auto ref=hollowDerivedReference(controlled,children[1],children[2],children.size()==4?children[3]:NodeId{},stop);if(!ref)return ref.error();}
 else {const auto& r=nodes->at(Document::rootId());const auto& source=nodes->at(children[1]);if(r.locked||!r.visible||r.opacity!=1||r.blend!=BlendMode::Normal||!r.maskId.empty()||r.transform!=LayerTransform{}||!hollowNeutralPaintLayer(nodes->at(children[0]))||!source.locked||!source.visible||source.opacity!=1||!source.maskId.empty()||source.transform!=LayerTransform{})return error(ErrorCode::UnsupportedFeature,"mixed.controlled-properties");
 auto raw=lineartRasterPixels(controlled,children[1]);if(!raw)return raw.error();
 for(std::size_t i=0;i<raw.value().size();i+=4){if(i%65536==0&&stop.stop_requested())return error(ErrorCode::Cancelled,"mixed.cancelled");const auto p=raw.value().subspan(i,4);if(p[3]&&(p[0]!=p[1]||p[1]!=p[2]))return error(ErrorCode::UnsupportedFeature,"mixed.grayscale-required");}
 for(const auto& e:*events){if(e.layerId!=children[0])return error(ErrorCode::UnsupportedFeature,"mixed.controlled-event");auto check=validateOriginalWhitePaint(e,controlled,raw.value(),stop);if(!check)return check.error();}}
 return MixedLineartView{std::move(controlled),std::move(finishing)};
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mixed.allocation");}
inline Result<Command> restoreControlledCommand(const DocumentSnapshot& original,const MixedLineartView& view,Command command,std::stop_token stop={}) try {
 auto actual=projectControlledLineart(original,stop);if(!actual)return actual.error();if(actual.value().finishing!=view.finishing||actual.value().controlled.revision!=view.controlled.revision||!actual.value().controlled.sameContent(view.controlled))return error(ErrorCode::InvalidInput,"mixed.stale-view");
 if(command.version!=1||command.operations.empty()||command.operations.size()>4)return error(ErrorCode::InvalidInput,"mixed.operations");
 std::set<Id> used;std::uint64_t tick=0;for(const auto& [id,n]:*original.nodes){(void)n;used.insert(id);}for(const auto& e:*original.eventIndex){used.insert(e.id);if(e.endUs==UINT64_MAX)return error(ErrorCode::Overflow,"mixed.timeline");tick=std::max(tick,e.endUs+1);}
 std::size_t count=view.controlled.nodes->at(Document::rootId()).children.size();std::set<NodeId> targets;for(const auto&[id,n]:*view.controlled.nodes){if(n.kind==NodeKind::Layer)targets.insert(id);}
 for(auto& op:command.operations){if(stop.stop_requested())return error(ErrorCode::Cancelled,"mixed.cancelled");if(auto* add=std::get_if<AddNode>(&op)){if(used.contains(add->node.id)||add->node.id.empty()||!hollowNeutralPaintLayer(add->node)||add->node.stage==FinishingLayerStage||add->node.composite||!add->node.provenance.empty())return error(ErrorCode::InvalidInput,"mixed.add-node");if(add->index==SIZE_MAX)add->index=count;if(add->index>count)return error(ErrorCode::InvalidInput,"mixed.add-index");++count;used.insert(add->node.id);targets.insert(add->node.id);}
 else if(auto* paint=std::get_if<Paint>(&op)){auto& e=paint->event;if(!targets.contains(e.layerId)||e.id.empty()||used.contains(e.id)||e.engineVersion!=8||e.geometry.empty())return error(ErrorCode::InvalidInput,"mixed.paint");auto valid=validateBucketEvent(e);if(!valid)return valid.error();const auto duration=e.endUs-e.startUs;if(duration>=UINT64_MAX-tick)return error(ErrorCode::Overflow,"mixed.timeline");const auto start=e.startUs;for(auto& p:e.geometry)p.timestampUs=tick+(p.timestampUs-start);e.startUs=tick;e.endUs=tick+duration;e.parentRevision=original.revision;tick=e.endUs+1;used.insert(e.id);}
 else return error(ErrorCode::UnsupportedFeature,"mixed.operation");}
 auto candidate=Document::fromSnapshot(original);if(!candidate)return candidate.error();auto applied=candidate.value().apply(command,original.revision);if(!applied)return applied.error();auto verified=projectControlledLineart(candidate.value().snapshot(),stop);if(!verified)return verified.error();return command;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mixed.allocation");}
inline Result<Command> makeFinishingLayerCommand(const DocumentSnapshot& s,NodeId id,std::stop_token stop={}) try {
 auto view=projectControlledLineart(s,stop);if(!view)return view.error();if(id.empty()||s.nodes->contains(id)||view.value().finishing.size()>=16)return error(ErrorCode::InvalidInput,"mixed.finishing-id-limit");for(const auto& e:*s.eventIndex)if(e.id==id)return error(ErrorCode::InvalidInput,"mixed.finishing-id");LayerNode n;n.id=id;n.parent=Document::rootId();n.name="Hand-painted details (not fill boundaries)";n.stage=FinishingLayerStage;n.bounds={0,0,s.spec.pixelSize.width,s.spec.pixelSize.height};return Command{AddNode{n,SIZE_MAX}};
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"mixed.allocation");}
}
