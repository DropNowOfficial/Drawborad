#include "brush/CubicPathStroke.h"
#include "brush/CubicInkStroke.h"
#include "brush/BucketFill.h"
#include "brush/SelectedBoundaryFill.h"
#include "brush/OriginalLineartFill.h"
#include "render/CoverageRasterizer.h"
#include "render/ReferenceRenderer.h"
#include "jobs/Protocol.h"
#include "storage/ProjectCodec.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace drawborad;
// Frozen pre-extraction23 subdivision, test-only golden reference.
inline Result<CubicInkGeometry> old23Golden(const StrokeEvent&e,std::stop_token stop={})try{
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 auto valid=validateCubicInkEvent(e);if(!valid)return valid.error();
 using namespace cubic_ink_detail;
 std::array<Point,4> controls;double scale=1;
 for(std::size_t i=0;i<4;++i){controls[i]={e.geometry[i].x,e.geometry[i].y};scale=std::max({scale,std::abs(controls[i].x),std::abs(controls[i].y)});}
 // Conservative <=16-level floating error margin, not an interval proof.
 const double margin=scale*std::numeric_limits<double>::epsilon()*4096;
 std::array<Node,18> stack;std::size_t pending=1;stack[0]={controls,0};
 CubicInkGeometry out;out.samples.push_back(e.geometry.front());
 while(pending){
  if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
  if(++out.visits>8191)return error(ErrorCode::BudgetExceeded,"cubic.flatten-work");
  const auto n=stack[--pending];const auto& p=n.c;
  const double bound=std::max(distance(p[1],blend(p[0],p[3],1./3)),distance(p[2],blend(p[0],p[3],2./3)))+margin;
  if(bound<=1./16){
   if(out.samples.size()>=4096)return error(ErrorCode::BudgetExceeded,"cubic.flatten-points");
   auto sample=e.geometry.front();sample.x=p[3].x;sample.y=p[3].y;out.samples.push_back(sample);continue;
  }
  if(n.depth>=16||pending+2>stack.size())return error(ErrorCode::BudgetExceeded,"cubic.flatten-depth");
  const auto a=blend(p[0],p[1],.5),b=blend(p[1],p[2],.5),d=blend(p[2],p[3],.5),f=blend(a,b,.5),g=blend(b,d,.5),h=blend(f,g,.5);
  stack[pending++]={{h,g,d,p[3]},n.depth+1};stack[pending++]={{p[0],a,f,h},n.depth+1};
 }
 if(stop.stop_requested())return error(ErrorCode::Cancelled,"cubic.cancelled");
 return out;
}catch(const std::bad_alloc&){return error(ErrorCode::BudgetExceeded,"cubic.allocation");}

struct Host final:WorkerHost{Result<void> start(const JobRequest&)override{return {};}void requestCancel(JobId)override{}void terminate(JobId)override{}};
struct TestClock final:Clock{std::uint64_t millis()const override{return 0;}};
Result<ChangeSet> external(Document&doc,const DocumentSnapshot&next){Host h;TestClock t;ResourceGovernor g;JobBroker broker(h,g,t);auto old=doc.snapshot();JobRequest q;q.id=Id::fromNumber(900);q.nonce=Id::fromNumber(901);q.task="test";q.resources.id=q.id;q.inputSnapshot={old.documentId,old.revision,stateHash(old).value()};q.snapshotBytes=encodeDelta(DocumentSnapshot{},old).value();assert(broker.submit(q));JobResult r;r.id=q.id;r.nonce=q.nonce;r.inputRevision=old.revision;r.delta=encodeDelta(old,next).value();r.outputHash=sha256(r.delta);return broker.accept(r,doc);}
int main(){
 for(unsigned k=0;k<64;++k){StrokeEvent e;e.engineVersion=23;e.toolId="cubic-ink-aa";e.style.width=4;for(unsigned j=0;j<4;++j)e.geometry.push_back({double((k*17+j*83)%512),double((k*61+j*43)%512),1,{},{},0,0,0});auto old=old23Golden(e),now=prepareCubicInkGeometry(e);assert(old&&now&&old.value().samples==now.value().samples&&old.value().visits==now.value().visits);}
 NewDocumentRequest request;request.dimensions={64,64,Unit::Px,300};auto spec=validateDocument(request,{});assert(spec);auto made=Document::create(spec.value(),Id::fromNumber(90));assert(made);auto& doc=made.value();const auto layer=Id::fromNumber(2);auto add=addFillLayerCommand(doc.snapshot(),layer);assert(add&&doc.apply(add.value(),0));auto blank=doc.snapshot();std::vector<std::array<double,2>> points{{32,8},{50,14},{56,48},{30,54},{4,48},{26,10}};ColorValue black;
 auto create=cubicPathStrokeCommand(blank,layer,Id::fromNumber(10),points,true,3,black);assert(create&&doc.apply(create.value(),blank.revision));auto original=doc.snapshot();assert(original.eventIndex->back().engineVersion==24&&original.eventIndex->back().geometry.size()==6);auto prepared=prepareCubicPathGeometry(original.eventIndex->back());assert(prepared&&prepared.value().samples.front()==prepared.value().samples.back());ReferenceRenderer r;auto pixels=r.renderRegion(original,layer,{0,0,64,64},{original.revision});assert(pixels);
 auto edit=adjustCubicPathControlCommand(original,Id::fromNumber(10),0,36,8);assert(edit&&doc.apply(edit.value(),original.revision));auto changed=doc.snapshot();for(std::size_t i=1;i<6;++i)assert(changed.eventIndex->back().geometry[i]==original.eventIndex->back().geometry[i]);auto edited=prepareCubicPathGeometry(changed.eventIndex->back());assert(edited&&edited.value().samples.front().x==36&&edited.value().samples.back().x==36);auto newPixels=r.renderRegion(changed,layer,{0,0,64,64},{changed.revision});assert(newPixels&&newPixels.value().bytes!=pixels.value().bytes);assert(doc.undo()&&doc.snapshot().sameContent(original));assert(doc.redo()&&doc.snapshot().sameContent(changed));
 const auto restoredChanged=doc.snapshot();auto openPoints=points;openPoints.push_back(points.front());auto open=cubicPathStrokeCommand(restoredChanged,layer,Id::fromNumber(11),openPoints,false,3,black);assert(open&&doc.apply(open.value(),restoredChanged.revision));auto withOpen=doc.snapshot();auto moveOpen=adjustCubicPathControlCommand(withOpen,Id::fromNumber(11),0,34,8);assert(moveOpen&&doc.apply(moveOpen.value(),withOpen.revision));auto openGeometry=prepareCubicPathGeometry(doc.snapshot().eventIndex->back());assert(openGeometry&&openGeometry.value().samples.front().x==34&&openGeometry.value().samples.back().x==32);
 auto current=doc.snapshot();assert(!adjustCubicPathControlCommand(current,Id::fromNumber(11),7,1,1));assert(!doc.apply(Command(AdjustCubicPathControl{Id::fromNumber(11),0,1e9,0}),current.revision));Command batch(AdjustCubicPathControl{Id::fromNumber(11),0,1,1});batch.operations.push_back(RenameNode{layer,"bad"});assert(!doc.apply(batch,current.revision)&&doc.snapshot().sameContent(current));
 ConvertColor conversion;conversion.target=current.spec.colorSpec;conversion.paperColor=current.spec.background.paperColor;conversion.events=current.eventIndex;assert(!doc.apply(Command(conversion),current.revision));auto fresh=Document::fromSnapshot(blank);assert(fresh&&!external(fresh.value(),original)&&fresh.value().snapshot().sameContent(blank));
 // Later overlapping raster-derived fill is a conservative dependency barrier, not auto-reflow.
 StrokeEvent bucket;bucket.id=Id::fromNumber(20);bucket.layerId=layer;bucket.objectId=current.nodes->at(layer).objectId;bucket.parentRevision=current.revision;bucket.engineVersion=8;bucket.toolId="bucket";bucket.geometry={{32,8,1,{},{},0,0,0},{34,8,1,{},{},0,0,0}};assert(doc.apply(Command(Paint{bucket,{32,8,2,1}}),current.revision));assert(!adjustCubicPathControlCommand(doc.snapshot(),Id::fromNumber(10),0,35,8));assert(doc.undo()&&doc.snapshot().sameContent(current));
 auto alias=original;auto events=std::make_shared<EventList>(*original.eventIndex);alias.eventIndex=events;ReferenceRenderer cached;auto first=cached.renderRegion(alias,layer,{0,0,64,64},{alias.revision});assert(first);events->front().geometry[1].x=40;auto second=cached.renderRegion(alias,layer,{0,0,64,64},{alias.revision});assert(second&&first.value().bytes!=second.value().bytes&&cached.statistics().cubicPreparations==2);events->front().color.channels[0]=1;auto colored=cached.renderRegion(alias,layer,{0,0,64,64},{alias.revision});assert(colored&&colored.value().bytes!=second.value().bytes);events->front().toolId="cubic-path-open-aa";assert(!cached.renderRegion(alias,layer,{0,0,64,64},{alias.revision}));events->front().toolId="cubic-path-closed-aa";auto restored=cached.renderRegion(alias,layer,{0,0,64,64},{alias.revision});assert(restored&&restored.value().bytes==colored.value().bytes);
 EventList mixed(128,original.eventIndex->back());assert(validateCubicDocumentBudget(mixed));auto simple=original.eventIndex->back();simple.engineVersion=23;simple.toolId="cubic-ink-aa";simple.geometry.resize(4);mixed.push_back(simple);auto over=validateCubicDocumentBudget(mixed);assert(!over&&over.error().messageKey=="cubic.curve-budget");CubicInkPreparation cache;auto many=std::make_shared<EventList>(mixed);assert(!cache.prepare(many));
 auto malformed=original.eventIndex->back();malformed.geometry[0].x=std::numeric_limits<double>::quiet_NaN();assert(!prepareCubicPathGeometry(malformed));malformed=original.eventIndex->back();malformed.geometry.resize(27,malformed.geometry[0]);assert(!validateCubicPathEvent(malformed));std::stop_source stop;stop.request_stop();assert(!prepareCubicPathGeometry(original.eventIndex->back(),stop.get_token()));
 // Production native24 is one opacity-union event, matching its unified19 surrogate.
 auto half=original;auto halfEvents=std::make_shared<EventList>(*original.eventIndex);halfEvents->back().style.opacity=.5;half.eventIndex=halfEvents;
 auto surrogate=half;auto surrogateEvents=std::make_shared<EventList>(*halfEvents);auto&se=surrogateEvents->back();se.engineVersion=19;se.toolId="smooth-ink-aa";se.geometry=prepared.value().samples;surrogate.eventIndex=surrogateEvents;
 ReferenceRenderer nr,sr;auto np=nr.renderRegion(half,layer,{0,0,64,64},{half.revision}),sp=sr.renderRegion(surrogate,layer,{0,0,64,64},{surrogate.revision});assert(np&&sp&&np.value().bytes==sp.value().bytes);
 auto nc=strokeCoverage(halfEvents->back(),{0,0,64,64},UINT64_MAX),sc=strokeCoverage(se,{0,0,64,64},UINT64_MAX);assert(nc&&sc&&nc.value()==sc.value());
 auto tile=nr.render(half,{layer,0,0,0},{half.revision});assert(tile);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64*4;++x)assert(tile.value().bytes[y*tile.value().rowStride+x]==np.value().bytes[y*np.value().rowStride+x]);
 // Real selected-boundary fill using native24 plus immutable original raster.
 auto fm=Document::create(spec.value(),Id::fromNumber(100));assert(fm);auto&fd=fm.value();const auto lower=Id::fromNumber(102),source=Id::fromNumber(103),boundary=Id::fromNumber(104);
 auto fl=addFillLayerCommand(fd.snapshot(),lower,true,true);assert(fl&&fd.apply(fl.value(),0));auto raster=makeLineartRasterCommand(Bytes(64*64*4,255),{'w'},64,64,source);assert(raster&&fd.apply(raster.value(),fd.snapshot().revision));auto detail=makeFinishingLayerCommand(fd.snapshot(),boundary);assert(detail&&fd.apply(detail.value(),fd.snapshot().revision));
 auto outline=cubicPathStrokeCommand(fd.snapshot(),boundary,Id::fromNumber(110),points,true,3,black);assert(outline&&fd.apply(outline.value(),fd.snapshot().revision));const auto beforeFill=fd.snapshot();ColorValue green;green.channels={0,1,0,0};auto fill=selectedBoundaryFillCommand(beforeFill,boundary,Id::fromNumber(111),32,30,green);assert(fill&&fd.apply(fill.value(),beforeFill.revision));const auto filled=fd.snapshot();assert(*filled.resources==*beforeFill.resources&&*filled.nodes==*beforeFill.nodes);for(std::size_t i=0;i<beforeFill.eventIndex->size();++i)assert((*filled.eventIndex)[i]==(*beforeFill.eventIndex)[i]);
 auto frame=renderMergedDocument(filled,{filled.revision});assert(frame);const auto& rgba=frame.value().merged;auto px=[&](unsigned x,unsigned y,unsigned c){return rgba.bytes[y*rgba.rowStride+x*4+c];};assert(px(32,30,0)==0&&px(32,30,1)==255&&px(32,30,2)==0);assert(px(2,2,0)==255&&px(2,2,1)==255&&px(2,2,2)==255);assert(!adjustCubicPathControlCommand(filled,Id::fromNumber(110),0,33,8));assert(fd.undo()&&fd.snapshot().sameContent(beforeFill));
 std::cout<<"PASS shared23 golden64, native24 open/closed shared control edit/pixels/Undo, conservative fill/worker/ConvertColor barriers, alias cache and mixed span budget. No UI or arbitrary-fill claim.\n";
}
