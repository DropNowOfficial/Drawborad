#include "brush/CubicInkStroke.h"
#include "brush/CubicPathStroke.h"
#include "core/EditableCubic.h"
#include "brush/CloseStrokeGap.h"
#include "brush/WithdrawClosedInkFill.h"
#include "QtBridge.h"
#include "AlphaImportPolicy.h"
#include "core/OriginalReference.h"
#include "brush/OriginalLineartFill.h"
#include "brush/MixedLineartStack.h"
#include "brush/SelectedBoundaryFill.h"
#include "brush/ClosedInkFill.h"
#include "brush/LocalStrokeEdit.h"
#include "core/FreehandEvent.h"
#include "core/EditableStroke.h"
#include "brush/FillRegionReview.h"
#include "brush/HollowInkCommand.h"
#include "brush/DerivedHollowFill.h"
#include "brush/ClipFillSelection.h"
#include "brush/ClipPolygonSelection.h"
#include "brush/CloseLineartGap.h"
#include "ui/MergedDocument.h"
#include <QFile>
#include <QColorSpace>
#include <QBuffer>
#include <cmath>
#include "storage/ProjectCodec.h"
#include "render/ReferenceRenderer.h"
#include "view/LayerTree.h"
#include "ui/WorkflowController.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUuid>
#include <algorithm>
namespace drawborad {
namespace {
Id uuid() {
    const auto b = QUuid::createUuid().toRfc4122();
    Id id;
    for (int i = 0; i < 16; i++) id.bytes[std::size_t(i)] = std::uint8_t(b[i]);
    return id;
}
QString layerText(NodeId id) {
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.bytes.data()), 16).toHex());
}
std::optional<NodeId> parseLayerId(const QString& text) {
    if (text.size() != 32) return std::nullopt;
    const auto bytes = QByteArray::fromHex(text.toLatin1());
    if (bytes.size() != 16 || QString::fromLatin1(bytes.toHex()) != text.toLower()) return std::nullopt;
    NodeId id;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()), 16, id.bytes.begin());
    return id;
}
}
QtBridge::QtBridge(QObject* parent)
    : QObject(parent),
      host_(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / L"DrawboradWorker.exe"),
      broker_(host_, governor_, clock_) {
    workflow_=new WorkflowController(*this);
    connect(workflow_,&WorkflowController::changed,this,&QtBridge::changed);
    poll_.setInterval(25);
    connect(&poll_, &QTimer::timeout, this, [this] {
        finishLocalStrokeEdit();
        broker_.poll();
        if (!running_) return;
        auto state = broker_.status(*running_);
        if (state == JobStatus::Cancelled || state == JobStatus::Failed) {
            running_.reset(); workerMessage_ = tr("Worker stopped. Document unchanged."); emit changed(); return;
        }
        auto m = host_.pollMessage(*running_);
        if (!m) { broker_.workerFailed(*running_); running_.reset(); workerMessage_=tr("Worker failed safely."); emit changed(); return; }
        if (m.value()) {
            if (m.value()->kind == MessageKind::Hello) { workerMessage_=tr("Native worker connected. You can move the window or cancel."); emit changed(); }
            if (m.value()->kind == MessageKind::Cancelled) { broker_.acknowledgeCancel(m.value()->id,m.value()->nonce); running_.reset(); workerMessage_=tr("Cancelled. Document unchanged."); emit changed(); }
        }
    });
    poll_.start();
}
QtBridge::~QtBridge(){fillStop_.request_stop();fillPool_.clear();fillPool_.waitForDone();}
void QtBridge::cancelFill(){if(cubicDraftActive()){cancelCubic();return;}if(fillPending()){pendingCloseStroke_=false;pendingWithdrawal_=false;pendingClosedInk_=false;pendingFill_.reset();pendingFillPreview_.reset();pendingFillSource_.reset();fillNotice_=tr("审阅已取消，原工程未修改。");emit changed();return;}if(fillBusy_){fillStop_.request_stop();fillNotice_=tr("正在取消区域操作；原图及已提交内容不变。");emit changed();}}
bool QtBridge::acceptFill(){
    if(!pendingFill_ || !pendingFillSource_)return false;
    const auto current=app_.snapshot();const auto source=*pendingFillSource_;
    if(!current || current->documentId!=source.documentId || current->revision!=source.revision || canvasGeneration_!=pendingFillGeneration_ || replaying()){cancelFill();fillNotice_=tr("工程发生变化，审阅结果未提交。");emit changed();return false;}
    const auto kind=pendingKind_;pendingKind_=PreviewKind::Existing;
    const bool closeStroke=pendingCloseStroke_;pendingCloseStroke_=false;
    const bool withdrawal=pendingWithdrawal_;pendingWithdrawal_=false;
    const bool closedInk=pendingClosedInk_;pendingClosedInk_=false;
    auto command=std::move(*pendingFill_);pendingFill_.reset();pendingFillPreview_.reset();pendingFillSource_.reset();
    std::optional<NodeId> paintedLayer;for(const auto& op:command.operations)if(const auto* p=std::get_if<Paint>(&op))paintedLayer=p->event.layerId;
    const auto retained=activeLayer_;const bool keepDetail=retained&&current->nodes->contains(*retained)&&current->nodes->at(*retained).stage==FinishingLayerStage&&nativeLayerPaintable(*current,*retained);
    const bool localEdit=command.operations.size()==1&&std::holds_alternative<AdjustStrokeSamples>(command.operations.front());
    auto committed=app_.apply(command);if(committed&&paintedLayer&&!keepDetail)activeLayer_=paintedLayer;if(committed && kind!=PreviewKind::Existing){cubicSource_.reset();cubicEvent_.reset();cubicLayer_.reset();emit cubicControlsChanged();}
    fillNotice_=committed?((kind==PreviewKind::CubicPathCreate||kind==PreviewKind::CubicPathEdit)?tr("已接受共享锚点轮廓；可撤销。首次请另存新文件，旧版不能读取24格式。闭合不保证可填色；后续填色可能锁定轮廓编辑。"):kind!=PreviewKind::Existing?tr("已接受原生四控点曲线；可撤销。首次保存请另存新文件，旧版不能读取23格式。"):closeStroke?tr("已追加所选笔迹的短首尾连接；原样本及其他笔迹未改，可撤销。端点合成外观会变化；过细或低压线仍可能不能填色。"):withdrawal?tr("已撤回最近已记录的完整闭合填色组；原图及其他笔迹保留，可撤销恢复。若保存提示格式不同，请另存新文件名，保留原文件。"):localEdit?tr("已接受原笔迹的局部修整；未新增笔迹或图层，颜色和时间不变，可撤销和保存。"):closedInk?tr("已接受闭合墨块内区填充及来源记录；原轮廓和其他层内容未改，可撤销。请首次另存新文件名，旧版不能打开；仅支持已核验记录组的受限撤回。半透明上层合成可能改变。"):tr("已接受区域操作，独立图层已更新；原线稿未改，可撤销和保存。")):QString::fromStdString(committed.error().messageKey);emit changed();return bool(committed);
}
QVariantList QtBridge::cubicControls() const {
    QVariantList out;if(!cubicSource_)return out;
    if(cubicPath_){for(const auto& p:cubicPathPoints_)out.push_back(QVariantMap{{"x",p[0]},{"y",p[1]}});}
    else for(const auto& p:cubicPoints_){out.push_back(QVariantMap{{"x",p[0]},{"y",p[1]}});}
    return out;
}
QVariantList QtBridge::editableCubics() const {
    const auto s=app_.snapshot();if(!s)return {};
    if(cubicMenuSource_&&cubicMenuSource_->documentId==s->documentId&&cubicMenuSource_->revision==s->revision&&cubicMenuSource_->nodes==s->nodes&&cubicMenuSource_->resources==s->resources&&cubicMenuSource_->eventIndex==s->eventIndex&&cubicMenuSource_->spec==s->spec&&cubicMenuSource_->fillOperations==s->fillOperations&&cubicMenuLayer_==activeLayer_)return cubicMenu_;
    QVariantList out;
    if(activeLayer_&&s->eventIndex->size()<=8192)for(auto it=s->eventIndex->rbegin();it!=s->eventIndex->rend()&&out.size()<128;++it)
        if(it->layerId==*activeLayer_&&((it->engineVersion==23&&editableCubicIndex(*s,it->id))||(it->engineVersion==24&&editableCubicPathIndex(*s,it->id))))out.push_back(QVariantMap{{"id",layerText(it->id)},{"name",(it->engineVersion==24?tr("共享锚点%1 · %2段（逐点编辑） ").arg(drawborad::cubicPathClosed(*it)?tr("闭合轮廓"):tr("开放路径")).arg(qulonglong(cubicPathSegmentCount(*it))):tr("原生四点曲线 "))+layerText(it->id).left(8)}});
    cubicMenuSource_=*s;cubicMenuLayer_=activeLayer_;cubicMenu_=out;return out;
}
bool QtBridge::beginCubicDraft(double width,const QColor& color){
    const auto s=app_.snapshot();if(!s||busy()||workflow_->pending()||replaying()||!activeLayer_||!nativeLayerPaintable(*s,*activeLayer_)||!std::isfinite(width)||width<.5||width>128||!color.isValid())return false;
    const double w=s->spec.pixelSize.width,h=s->spec.pixelSize.height;
    cubicPath_=false;cubicPathEditIndex_.reset();cubicPathPoints_.clear();
    cubicPoints_={{{w*.2,h*.7},{w*.2,h*.3},{w*.8,h*.3},{w*.8,h*.7}}};
    cubicSource_=*s;cubicEvent_.reset();cubicLayer_=*activeLayer_;cubicWidth_=width;cubicColor_=color;
    fillNotice_=tr("四点曲线草稿：拖动端点或控制点；放开后精确预览，接受前不改工程。");emit cubicControlsChanged();emit changed();return true;
}
bool QtBridge::beginCubicPathDraft(double width,const QColor& color){
    const auto s=app_.snapshot();if(!s||busy()||workflow_->pending()||replaying()||!activeLayer_||!nativeLayerPaintable(*s,*activeLayer_)||!std::isfinite(width)||width<.5||width>128||!color.isValid())return false;
    const double w=s->spec.pixelSize.width,h=s->spec.pixelSize.height;
    cubicPathPoints_={{w*.5,h*.2},{w*.8,h*.3},{w*.85,h*.65},{w*.5,h*.8},{w*.2,h*.65},{w*.25,h*.3}};
    cubicPath_=true;cubicPathClosed_=true;cubicPathEditIndex_.reset();cubicSource_=*s;cubicEvent_.reset();cubicLayer_=*activeLayer_;cubicWidth_=width;cubicColor_=color;
    fillNotice_=tr("两段闭合轮廓草稿：蓝色共享锚点、黄色独立手柄。实际为固定圆笔描边，不是自动挖空或保证可填色；接受前可调全部六点。");emit cubicControlsChanged();emit changed();return true;
}
bool QtBridge::selectCubic(const QString& text){
    const auto s=app_.snapshot();const auto id=parseLayerId(text);if(!s||!id||busy()||workflow_->pending()||replaying())return false;
    auto found=std::find_if(s->eventIndex->begin(),s->eventIndex->end(),[&](const auto&e){return e.id==*id;});if(found==s->eventIndex->end())return false;
    const bool path=found->engineVersion==24;
    auto index=path?editableCubicPathIndex(*s,*id):editableCubicIndex(*s,*id);if(!index)return false;const auto& e=(*s->eventIndex)[index.value()];if(!activeLayer_||e.layerId!=*activeLayer_)return false;
    cubicSource_=*s;cubicEvent_=*id;cubicLayer_=e.layerId;cubicPath_=path;cubicPathEditIndex_.reset();cubicPathPoints_.clear();
    if(path){cubicPathClosed_=drawborad::cubicPathClosed(e);for(const auto&p:e.geometry)cubicPathPoints_.push_back({p.x,p.y});}
    else for(std::size_t i=0;i<4;++i)cubicPoints_[i]={e.geometry[i].x,e.geometry[i].y};
    fillNotice_=path?tr("已选共享锚点轮廓：本次只能调整一个点，可反复微调同一点；接受或取消后才能改另一点，不改变开闭拓扑。"):tr("已选择原生四点曲线；拖柄后预览，接受前原笔迹不变。");emit cubicControlsChanged();emit changed();return true;
}
bool QtBridge::moveCubicControl(int i,double x,double y){
    if(!cubicSource_||fillBusy_||fillPending()||i<0||std::size_t(i)>=(cubicPath_?cubicPathPoints_.size():4)||!std::isfinite(x)||!std::isfinite(y))return false;
    const double w=cubicSource_->spec.pixelSize.width,h=cubicSource_->spec.pixelSize.height;
    if(x < -w||x>2*w||y < -h||y>2*h)return false;
    if(cubicPath_){
        if(cubicEvent_&&cubicPathEditIndex_&&*cubicPathEditIndex_!=std::size_t(i)){fillNotice_=tr("本次只能调整已改动的那个点；请先接受或取消，其他点未改变。");emit changed();return false;}
        cubicPathPoints_[std::size_t(i)]={x,y};
        if(cubicEvent_){const auto& events=*cubicSource_->eventIndex;const auto original=std::find_if(events.begin(),events.end(),[&](const auto&e){return e.id==*cubicEvent_;});const auto& p=original->geometry[std::size_t(i)];if(x==p.x&&y==p.y)cubicPathEditIndex_.reset();else cubicPathEditIndex_=std::size_t(i);}
    }else cubicPoints_[std::size_t(i)]={x,y};emit cubicControlsChanged();return true;
}
bool QtBridge::resumeCubicDraft(){
    if(!cubicDraftActive()||fillBusy_||!fillPending()||pendingKind_==PreviewKind::Existing)return false;
    pendingFill_.reset();pendingFillSource_.reset();pendingFillPreview_.reset();pendingKind_=PreviewKind::Existing;
    fillNotice_=tr("已返回调柄，全部草稿点保留；尚未提交，仍须预览后接受或明确放弃。");emit changed();return true;
}
void QtBridge::cancelCubic(){
    if(!cubicDraftActive())return;
    fillStop_.request_stop();cubicSource_.reset();cubicEvent_.reset();cubicLayer_.reset();
    pendingFill_.reset();pendingFillSource_.reset();pendingFillPreview_.reset();pendingKind_=PreviewKind::Existing;
    fillNotice_=tr("曲线草稿已取消，已提交工程未修改。");emit cubicControlsChanged();emit changed();
}
bool QtBridge::previewCubic() try {
    if(!cubicSource_||!cubicLayer_||fillBusy_||fillPending()||strokeCommitBusy_||running_||workflow_->pending()||replaying())return false;
    const auto current=app_.snapshot();const auto source=*cubicSource_;
    if(!current||current->documentId!=source.documentId||current->revision!=source.revision){cancelCubic();return false;}
    const auto points=cubicPoints_;const auto pathPoints=cubicPathPoints_;const bool path=cubicPath_;const bool closed=cubicPathClosed_;const auto editIndex=cubicPathEditIndex_;
    if(path&&cubicEvent_&&!editIndex){fillNotice_=tr("尚未调整任何控制点；请调一个点再预览，或取消草稿。");emit changed();return false;}
    const auto event=cubicEvent_;const auto layer=*cubicLayer_;const auto id=uuid();const auto width=cubicWidth_;const auto generation=canvasGeneration_;
    ColorValue color;color.activeChannels=3;color.profileId=source.spec.colorSpec.profileId;color.channels={cubicColor_.redF(),cubicColor_.greenF(),cubicColor_.blueF(),0};color.alpha=cubicColor_.alphaF();
    fillPool_.setMaxThreadCount(1);fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();
    {std::lock_guard lock(localEditMutex_);localEditCompletion_.reset();}
    fillBusy_=true;fillNotice_=tr("正在验收原生曲线预览；可取消，尚未提交。");emit changed();
    fillPool_.start([this,source,points,pathPoints,path,closed,editIndex,event,layer,id,width,generation,color,stop]{
        LocalEditCompletion done{std::nullopt,source,std::nullopt,{},stop,generation};done.kind=path?(event?PreviewKind::CubicPathEdit:PreviewKind::CubicPathCreate):(event?PreviewKind::CubicEdit:PreviewKind::CubicCreate);
        try{
            auto command=path?(event?adjustCubicPathControlCommand(source,*event,*editIndex,pathPoints[*editIndex][0],pathPoints[*editIndex][1],stop):cubicPathStrokeCommand(source,layer,id,pathPoints,closed,width,color,1,stop)):(event?adjustCubicControlsCommand(source,*event,points,stop):cubicInkStrokeCommand(source,layer,id,points,width,color,1,stop));
            if(!command)done.failure=QString::fromStdString(command.error().messageKey);
            else {auto candidate=Document::fromSnapshot(source);if(!candidate)done.failure=QString::fromStdString(candidate.error().messageKey);
                else {auto applied=candidate.value().apply(command.value(),source.revision);if(!applied)done.failure=QString::fromStdString(applied.error().messageKey);
                    else {auto preview=candidate.value().snapshot();auto frame=renderMergedDocument(preview,{preview.revision},{},stop);
                        if(!frame)done.failure=QString::fromStdString(frame.error().messageKey);else{done.command=std::move(command.value());done.preview=std::move(preview);}}}}
        }catch(const std::bad_alloc&){done.failure=QStringLiteral("cubic.preview-allocation");}
        std::lock_guard lock(localEditMutex_);localEditCompletion_=std::move(done);
    });return true;
}catch(const std::bad_alloc&){fillBusy_=false;fillNotice_=tr("曲线预览内存不足，草稿保留。");emit changed();return false;}
void QtBridge::refreshEditCache() const {
    const auto s=app_.snapshot();
    if(!s)return;
    if(editCacheSource_&&editCacheSource_->documentId==s->documentId&&editCacheSource_->revision==s->revision&&editCacheSource_->nodes==s->nodes&&editCacheSource_->resources==s->resources&&editCacheSource_->eventIndex==s->eventIndex&&editCacheSource_->spec==s->spec&&editCacheSource_->fillOperations==s->fillOperations&&editCacheLayer_==activeLayer_)return;
    QVariantList out;
    if(activeLayer_&&s->eventIndex->size()<=8192&&nativeLayerPaintable(*s,*activeLayer_)){
        std::uint64_t points=0;bool bounded=true;for(const auto& e:*s->eventIndex){if(e.geometry.size()>1000000-points){bounded=false;break;}points+=e.geometry.size();}
        if(bounded)for(std::size_t i=s->eventIndex->size();i>0&&out.size()<128;--i){const auto& e=(*s->eventIndex)[i-1];
            if(!independentNativeStroke(*s,e))break;
            if(e.layerId==*activeLayer_&&e.engineVersion==19&&e.geometry.size()<=4096)out.push_back(QVariantMap{{"id",layerText(e.id)},{"label",tr("笔迹 %1 · %2 个采样点").arg(qulonglong(i)).arg(qulonglong(e.geometry.size()))}});
        }
    }
    editCacheMenu_=std::move(out);editCacheSource_=s;editCacheLayer_=activeLayer_;editCachePointId_.reset();editCachePoints_.clear();
}
QVariantList QtBridge::editableStrokes() const try {
    if(!app_.snapshot()||busy()||workflow_->pending()||replaying())return {};
    refreshEditCache();return editCacheMenu_;
}catch(const std::bad_alloc&){return {};}
std::optional<Id> QtBridge::editStrokeId() const {
    const auto list=editableStrokes();if(list.isEmpty())return std::nullopt;
    if(selectedEditStroke_){for(const auto& v:list)if(v.toMap()["id"].toString()==layerText(*selectedEditStroke_))return selectedEditStroke_;return std::nullopt;}
    return parseLayerId(list.front().toMap()["id"].toString());
}
QString QtBridge::selectedEditStroke() const {const auto id=editStrokeId();return id?layerText(*id):QString{};}
bool QtBridge::selectEditStroke(const QString& text) {
    if(busy()||workflow_->pending())return false;
    const auto id=parseLayerId(text);if(!id)return false;
    for(const auto& v:editableStrokes())if(v.toMap()["id"].toString()==text){selectedEditStroke_=id;emit changed();return true;}
    fillNotice_=tr("该笔迹不在当前层可安全修整范围；有后续区域操作的旧笔迹不会被自动改写。");emit changed();return false;
}
QVariantList QtBridge::localStrokePoints() const try {
    const auto s=app_.snapshot();if(!s||busy()||workflow_->pending()||replaying())return {};
    const auto id=editStrokeId();if(!id)return {};
    // This is read-only display data from the immutable cached eligibility menu.
    // The worker and command boundary still independently validate the full snapshot.
    if(editCachePointId_==id)return editCachePoints_;
    const auto found=std::find_if(s->eventIndex->begin(),s->eventIndex->end(),[&](const auto& e){return e.id==*id;});
    if(found==s->eventIndex->end())return {};
    QVariantList out;out.reserve(qsizetype(found->geometry.size()));for(const auto& p:found->geometry)out.push_back(QVariantMap{{"x",p.x},{"y",p.y},{"pressure",p.pressure}});
    editCachePoints_=std::move(out);editCachePointId_=id;return editCachePoints_;
}catch(const std::bad_alloc&){return {};}
bool QtBridge::previewLocalStrokeEdit(int sampleIndex,double radius,double dx,double dy,double pressureDelta) try {
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    const auto s=app_.snapshot();
    if(!s||busy()||workflow_->pending()||replaying()||sampleIndex<0||s->eventIndex->empty())return fail(tr("请先结束当前任务，并选择连续实墨的有效样本。"));
    const auto id=editStrokeId();if(!id)return fail(tr("请选择当前层可安全修整的实墨笔迹；其后若有填色或挖空则不可修整。"));
    const auto index=editableStrokeIndex(*s,*id);if(!index)return fail(QString::fromStdString(index.error().messageKey));
    const auto& last=(*s->eventIndex)[index.value()];
    if(last.engineVersion!=19||last.toolId!="smooth-ink-aa"||std::size_t(sampleIndex)>=last.geometry.size()||last.geometry.size()>4096||!nativeLayerPaintable(*s,last.layerId))return fail(tr("本次只允许修整当前层可安全修整的连续实墨；工程未改。"));
    if(!std::isfinite(radius)||radius<=0||radius>128||!std::isfinite(dx)||!std::isfinite(dy)||std::abs(dx)>128||std::abs(dy)>128||!std::isfinite(pressureDelta)||std::abs(pressureDelta)>1)return fail(tr("半径须为0到128之间的正数，位移每轴≤128原像素，压力增量绝对值≤1。"));
    const auto snapshot=*s;const auto eventId=last.id;const auto generation=canvasGeneration_;
    fillPool_.setMaxThreadCount(1);fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();
    {std::lock_guard lock(localEditMutex_);localEditCompletion_.reset();}
    fillBusy_=true;fillNotice_=tr("正在按原笔迹弧长计算局部修整并验收画面；尚未提交，可取消。范围外样本和原图不改。");emit changed();
    fillPool_.start([this,snapshot,eventId,generation,stop,sampleIndex,radius,dx,dy,pressureDelta]{
        LocalEditCompletion done{std::nullopt,snapshot,std::nullopt,{},stop,generation};
        try {
            auto command=localStrokeEditCommand(snapshot,eventId,std::size_t(sampleIndex),radius,dx,dy,pressureDelta,stop);
            if(!command)done.failure=QString::fromStdString(command.error().messageKey);
            else {
                auto candidate=Document::fromSnapshot(snapshot);
                if(!candidate)done.failure=QString::fromStdString(candidate.error().messageKey);
                else {
                    auto applied=candidate.value().apply(command.value(),snapshot.revision);
                    if(!applied)done.failure=QString::fromStdString(applied.error().messageKey);
                    else {
                        const auto edited=candidate.value().snapshot();const auto editedIndex=editableStrokeIndex(edited,eventId,stop);
                        if(!editedIndex){done.failure=QString::fromStdString(editedIndex.error().messageKey);}const auto layer=editedIndex?(*edited.eventIndex)[editedIndex.value()].layerId:NodeId{};
                        if(editedIndex&&edited.nodes->at(layer).stage==FinishingLayerStage){auto projected=projectControlledLineart(edited,stop);if(!projected)done.failure=QString::fromStdString(projected.error().messageKey);}
                        if(done.failure.isEmpty()){
                            auto frame=renderMergedDocument(edited,{edited.revision},{},stop);
                            if(!frame)done.failure=QString::fromStdString(frame.error().messageKey);
                            else {done.command=std::move(command.value());done.preview=edited;}
                        }
                    }
                }
            }
        }catch(const std::bad_alloc&){done.failure=QStringLiteral("stroke-edit.preview-allocation");}
        // Busy remains true until this bounded mailbox is drained, including
        // cancellation. No queued callback allocation can strand the operation.
        std::lock_guard lock(localEditMutex_);localEditCompletion_=std::move(done);
    });
    return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("局部修整启动内存不足，工程未修改。");emit changed();return false;}
bool QtBridge::previewClosedInkFill() try {
    const auto fail=[&](const QString& why){fillNotice_=why;emit changed();return false;};
    const auto s=app_.snapshot();
    if(!s||busy()||workflow_->pending()||replaying()||!activeLayer_)return fail(tr("请先结束当前操作，再选一个手绘细节层。"));
    const auto snapshot=*s;const auto selected=*activeLayer_;const auto generation=canvasGeneration_;
    const std::array<Id,4> ids{uuid(),uuid(),uuid(),uuid()};fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();fillPool_.setMaxThreadCount(1);
    {std::lock_guard lock(localEditMutex_);localEditCompletion_.reset();}
    fillBusy_=true;fillNotice_=tr("正在验证单笔闭合实墨并渲染内区覆盖；不自动补开口，尚未提交，可取消。");emit changed();
    fillPool_.start([this,snapshot,selected,ids,generation,stop]{
        LocalEditCompletion done{std::nullopt,snapshot,std::nullopt,{},stop,generation,true};
        try {
            auto command=closedInkFillCommand(snapshot,selected,ids,stop);
            if(!command)done.failure=QString::fromStdString(command.error().messageKey);
            else {auto candidate=Document::fromSnapshot(snapshot);if(!candidate)done.failure=QString::fromStdString(candidate.error().messageKey);
                else {auto applied=candidate.value().apply(command.value(),snapshot.revision);if(!applied)done.failure=QString::fromStdString(applied.error().messageKey);
                    else {const auto edited=candidate.value().snapshot();auto frame=renderMergedDocument(edited,{edited.revision},{},stop);if(!frame)done.failure=QString::fromStdString(frame.error().messageKey);else {done.command=std::move(command.value());done.preview=edited;}}
                }
            }
        }catch(const std::bad_alloc&){done.failure=QStringLiteral("closed-ink.preview-allocation");}
        std::lock_guard lock(localEditMutex_);localEditCompletion_=std::move(done);
    });
    return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("闭合墨块验收启动内存不足，工程未修改。");emit changed();return false;}
bool QtBridge::previewWithdrawClosedInkFill() try {
    const auto fail=[&](const QString& why){fillNotice_=why;emit changed();return false;};
    const auto current=app_.snapshot();
    if(!current||busy()||workflow_->pending()||replaying()||current->fillOperations.empty())return fail(tr("没有可选择的已记录闭合填色组，或当前有未完成操作。旧色块不会猜组撤回。"));
    const auto snapshot=*current;const auto id=snapshot.fillOperations.back().id;const auto generation=canvasGeneration_;
    fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();fillPool_.setMaxThreadCount(1);
    {std::lock_guard lock(localEditMutex_);localEditCompletion_.reset();}
    fillBusy_=true;fillNotice_=tr("正在重算最近记录组及检查后续依赖，尚未删除；可取消。未知填色、条件变化或配方不符均拒绝。");emit changed();
    fillPool_.start([this,snapshot,id,generation,stop]{
        LocalEditCompletion done{std::nullopt,snapshot,std::nullopt,{},stop,generation,false,true};
        try {
            auto command=withdrawClosedInkFillCommand(snapshot,id,stop);
            if(!command)done.failure=QString::fromStdString(command.error().messageKey);
            else {auto candidate=Document::fromSnapshot(snapshot);if(!candidate)done.failure=QString::fromStdString(candidate.error().messageKey);
                else {auto applied=candidate.value().apply(command.value(),snapshot.revision);if(!applied)done.failure=QString::fromStdString(applied.error().messageKey);
                    else {const auto preview=candidate.value().snapshot();auto frame=renderMergedDocument(preview,{preview.revision},{},stop);if(!frame)done.failure=QString::fromStdString(frame.error().messageKey);else {done.command=std::move(command.value());done.preview=preview;}}}}
        }catch(const std::bad_alloc&){done.failure=tr("内存预算不足，工程未改。");}
        if(stop.stop_requested())done.failure=tr("撤回验收已取消，工程未改。");
        {std::lock_guard lock(localEditMutex_);localEditCompletion_=std::move(done);}
    });return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("撤回验收启动失败，工程未改。");emit changed();return false;}
bool QtBridge::previewCloseStrokeGap() try {
    const auto fail=[&](const QString& why){fillNotice_=why;emit changed();return false;};
    const auto current=app_.snapshot();const auto chosen=editStrokeId();
    if(!current||busy()||workflow_->pending()||replaying()||!chosen)return fail(tr("请先选择可编辑的连续实墨笔迹；已填色依赖需先安全撤回，不能强行改线。"));
    const auto snapshot=*current;const auto id=*chosen;const auto generation=canvasGeneration_;
    fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();fillPool_.setMaxThreadCount(1);
    {std::lock_guard lock(localEditMutex_);localEditCompletion_.reset();}
    fillBusy_=true;fillNotice_=tr("正在验证短缺口闭合并渲染候选，未改原笔迹；可取消。");emit changed();
    fillPool_.start([this,snapshot,id,generation,stop]{
        LocalEditCompletion done{std::nullopt,snapshot,std::nullopt,{},stop,generation,false,false,true};
        try {
            auto command=closeStrokeGapCommand(snapshot,id,stop);
            if(!command)done.failure=QString::fromStdString(command.error().messageKey);
            else {auto candidate=Document::fromSnapshot(snapshot);if(!candidate)done.failure=QString::fromStdString(candidate.error().messageKey);
                else {auto applied=candidate.value().apply(command.value(),snapshot.revision);if(!applied)done.failure=QString::fromStdString(applied.error().messageKey);
                    else {const auto preview=candidate.value().snapshot();auto frame=renderMergedDocument(preview,{preview.revision},{},stop);if(!frame)done.failure=QString::fromStdString(frame.error().messageKey);else {done.command=std::move(command.value());done.preview=preview;}}}}
        }catch(const std::bad_alloc&){done.failure=tr("内存预算不足，工程未改。");}
        if(stop.stop_requested())done.failure=tr("闭合验收已取消，工程未改。");
        {std::lock_guard lock(localEditMutex_);localEditCompletion_=std::move(done);}
    });return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("闭合验收启动失败，工程未改。");emit changed();return false;}
void QtBridge::finishLocalStrokeEdit() try {
    std::optional<LocalEditCompletion> done;
    {std::lock_guard lock(localEditMutex_);if(!localEditCompletion_)return;done=std::move(localEditCompletion_);localEditCompletion_.reset();}
    fillBusy_=false;const auto current=app_.snapshot();
    if(done->stop.stop_requested()){fillNotice_=tr("操作已取消，原工程未修改。");emit changed();return;}
    if(!current||replaying()||current->documentId!=done->source.documentId||current->revision!=done->source.revision||canvasGeneration_!=done->generation){fillNotice_=tr("工程已改变，旧操作结果已丢弃。");emit changed();return;}
    if(!done->failure.isEmpty()||!done->command||!done->preview){fillNotice_=(done->kind!=PreviewKind::Existing?tr("曲线预览被拒绝，草稿保留，工程未改："):done->closeStroke?tr("闭合被拒绝，原工程未改："):done->withdrawal?tr("撤回被拒绝，原工程未改："):done->closedInk?tr("闭合墨块填充被拒绝，原工程未改："):tr("局部修整未提交，工程未改："))+done->failure;emit changed();return;}
    pendingKind_=done->kind;
    pendingFill_=std::move(done->command);pendingFillSource_=std::move(done->source);pendingFillPreview_=std::move(done->preview);pendingFillGeneration_=done->generation;pendingClosedInk_=done->closedInk;pendingWithdrawal_=done->withdrawal;pendingCloseStroke_=done->closeStroke;
    fillNotice_=done->kind!=PreviewKind::Existing?tr("原生曲线精确预览，尚未提交。可接受、返回调柄或放弃；保留原控制点，不转成采样折线。"):done->closeStroke?tr("短缺口闭合预览，尚未提交：只追加所选笔迹首尾之间的直线连接，不挪动或简化原样本。连接≤32原像素且≤路径5%，自交拒绝。请看端点形状后接受或取消；闭合不保证低压/过细线可填色。"):done->withdrawal?tr("撤回预览尚未提交：删除最近已记录闭合填色组的全部底色成员，保留原线稿和其后独立手绘；半透明合成可能改变。接受时须同步重算，可能短暂等待。若移除最后记录，须另存新文件名保留原20格式文件。请明确接受或取消。"):done->closedInk?tr("闭合墨块预览，尚未提交：以所选单笔墨色填几何内区，原轮廓及其他层内容不变；半透明上层可透出底色。接受后记录来源并采用新格式，首次须另存全新文件名，旧版不能打开；来源边界/成员受保护，仅支持已核验记录组的受限撤回。请接受或取消。"):tr("局部修整预览，尚未提交：仅沿原笔迹弧长附近调整位置/压力，未新增图层；范围外样本、颜色及时间不变。请接受或取消。");emit changed();
}catch(const std::bad_alloc&){fillBusy_=false;pendingCloseStroke_=false;pendingWithdrawal_=false;pendingClosedInk_=false;pendingFill_.reset();pendingFillSource_.reset();pendingFillPreview_.reset();fillNotice_=tr("预览内存不足，未提交工程。");emit changed();}
bool QtBridge::dirty() const {return cubicDraftActive() || app_.dirty() || workflow_->pending() || fillPending();}
int QtBridge::eventCount() const {const auto s=app_.snapshot();return s?int(std::min<std::size_t>(s->eventIndex->size(),std::size_t(INT_MAX))):0;}
int QtBridge::replayCount() const {return replayCount_<0?eventCount():std::min(replayCount_,eventCount());}
void QtBridge::setReplayCount(int count){if(cubicDraftActive() || strokeCommitBusy_ || fillBusy_ || fillPending())return;count=std::clamp(count,0,eventCount());replayCount_=count==eventCount()?-1:count;emit changed();}
bool QtBridge::undoDrawing(){
    if(busy() || workflow_->pending() || !app_.undo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::redoDrawing(){
    if(busy() || workflow_->pending() || !app_.redo())return false;
    replayCount_=-1;++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());emit changed();return true;
}
bool QtBridge::busy() const {return cubicDraftActive() || strokeCommitBusy_ || fillBusy_ || fillPending() || running_.has_value() || workflow_->busy();}
QObject* QtBridge::workflow() const {return workflow_;}
QString QtBridge::message() const {
    const auto& key=app_.message();
    if(key=="save.format-copy-required"||key=="save.clean-copy-required")return tr("未保存：请另存为一个全新的 .draw 文件名（无旧备份或日志），保留原文件。当前工程的新格式不能覆盖旧格式，旧版程序不能打开；不要删除原文件来绕过保护。");
    return !key.empty()?QString::fromStdString(key):workerMessage_;
}
QString QtBridge::fillProvenanceNotice() const {
    const auto s=app_.snapshot();if(!s)return {};
    if(s->fillOperations.empty())return tr("未记录可返工的填色来源；旧色块不按颜色或时间猜分组。当前仍不能安全地单区撤回重填。");
    auto text=tr("已持久记录 %1 次闭合实墨填色的成员与来源。请首次另存全新 .draw 副本，旧版程序不能打开；原边界/成员受保护，只有最新记录组可预览受限撤回，不自动重算画作。其他未记录色块仍不可推断。").arg(s->fillOperations.size());
    text+=tr(" 记录只表示当时条件；撤回必须重新验算配方及后续依赖，无法证明则拒绝。");
    return text;
}
QString QtBridge::dimensions() const { auto s=app_.snapshot(); if(!s)return tr("No document"); return QString::number(s->spec.pixelSize.width)+" x "+QString::number(s->spec.pixelSize.height)+tr(" px / revision ")+QString::number(s->revision); }
QVariantList QtBridge::layers() const {
    QVariantList result;
    const auto snapshot = app_.snapshot();
    if (!snapshot) return result;
    const auto order = layerOrder(*snapshot);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto& node = snapshot->nodes->at(*it);
        result.push_back(QVariantMap{{"id", layerText(*it)}, {"name", QString::fromStdString(node.name)},
            {"visible", node.visible}, {"locked", layerLocked(*snapshot, *it)},
            {"supported", nativeLayerSupported(*snapshot, *it)},
            {"paintable", nativeLayerPaintable(*snapshot, *it)}});
    }
    return result;
}
QString QtBridge::activeLayerId() const { return activeLayer_ ? layerText(*activeLayer_) : QString{}; }
void QtBridge::resetLayerSelection() {
    selectedEditStroke_.reset();editCacheSource_.reset();editCacheLayer_.reset();editCacheMenu_.clear();editCachePointId_.reset();editCachePoints_.clear();
    activeLayer_.reset();
    const auto snapshot = app_.snapshot();
    if (!snapshot) return;
    const auto order = layerOrder(*snapshot);
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if (nativeLayerPaintable(*snapshot, *it)) { activeLayer_ = *it; return; }
    if (!order.empty()) activeLayer_ = order.back();
}
bool QtBridge::canAddLayer() const {
    if(busy() || workflow_->pending())return false;
    const auto snapshot = app_.snapshot();
    if (!snapshot || snapshot->spec.colorSpec.format != PixelFormat{ColorMode::RGB, Depth::U8, true}) return false;
    auto parent = Document::rootId();
    if (activeLayer_ && snapshot->nodes->contains(*activeLayer_)) parent = snapshot->nodes->at(*activeLayer_).parent;
    return layerWritable(*snapshot, parent) && layerVisible(*snapshot, parent) &&
        nativeCompositingSupported(*snapshot, parent);
}
QString QtBridge::layerNotice() const {
    const auto snapshot = app_.snapshot();
    if (!snapshot) return {};
    for (const auto& [id, node] : *snapshot->nodes)
        if (layerVisible(*snapshot, id) &&
            (node.kind == NodeKind::Layer ? !nativeLayerSupported(*snapshot, id)
                                          : !nativeCompositingSupported(*snapshot, id)))
            return tr("Some content cannot be displayed by this canvas.");
    if (!activeLayer_ || !nativeLayerPaintable(*snapshot, *activeLayer_))
        return tr("Select an editable, visible paint layer to draw.");
    return {};
}
bool QtBridge::selectLayer(const QString& text) {
    if(busy()||workflow_->pending())return false;
    const auto snapshot = app_.snapshot();
    const auto id = parseLayerId(text);
    if (!snapshot || !id || !snapshot->nodes->contains(*id) || snapshot->nodes->at(*id).kind != NodeKind::Layer)
        return false;
    if (activeLayer_ == id) return true;
    activeLayer_ = id;selectedEditStroke_.reset();
    emit changed();
    return true;
}
bool QtBridge::addLayer() {
    const auto snapshot = app_.snapshot();
    if (!snapshot || !canAddLayer()) return false;
    Command command;
    NodeId parent, objectId;
    std::size_t index = SIZE_MAX;
    if (activeLayer_ && snapshot->nodes->contains(*activeLayer_)) {
        const auto& selected = snapshot->nodes->at(*activeLayer_);
        parent = selected.parent;
        objectId = selected.objectId;
        const auto& siblings = snapshot->nodes->at(parent).children;
        index = std::size_t(std::find(siblings.begin(), siblings.end(), *activeLayer_) - siblings.begin()) + 1;
    }
    if (objectId.empty()) {
        LayerNode object;
        object.id = uuid(); object.parent = Document::rootId(); object.kind = NodeKind::Object;
        object.name = "Artwork";
        object.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
        parent = objectId = object.id;
        command.operations.push_back(AddNode{object});
        index = SIZE_MAX;
    }
    LayerNode layer;
    layer.id = uuid(); layer.parent = parent; layer.objectId = objectId;
    layer.name = tr("Layer %1").arg(qulonglong(layerOrder(*snapshot).size() + 1)).toUtf8().toStdString();
    layer.stage = "paint";
    layer.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
    command.operations.push_back(AddNode{layer, index});
    const auto result = app_.apply(command);
    if (result) {activeLayer_ = layer.id;selectedEditStroke_.reset();}
    emit changed();
    return bool(result);
}
bool QtBridge::addFinishingLayer() try {
 const auto s=app_.snapshot();if(!s||busy()||workflow_->pending()||replaying())return false;
 const auto id=uuid();auto command=makeFinishingLayerCommand(*s,id);
 if(!command){fillNotice_=tr("未创建细节层：需要兼容的原线稿区域工程；不会自动改写原图层。 ")+QString::fromStdString(command.error().messageKey);emit changed();return false;}
 for(auto& op:command.value().operations)if(auto* layer=std::get_if<AddNode>(&op))layer->node.name=tr("手绘细节（不作填色边界）").toUtf8().toStdString();
 auto done=app_.apply(command.value());if(done){activeLayer_=id;selectedEditStroke_.reset();fillNotice_=tr("细节层已创建。其笔触原样保留，但不参与区域边界；区域填色仍按原线稿/已接受补线计算。");}else fillNotice_=QString::fromStdString(done.error().messageKey);emit changed();return bool(done);
}catch(const std::bad_alloc&){fillNotice_=tr("细节层创建内存不足，请核对工程状态。");emit changed();return false;}
bool QtBridge::setLayerVisible(const QString& text, bool visible) {
    if(busy() || workflow_->pending())return false;
    const auto snapshot = app_.snapshot();
    const auto id = parseLayerId(text);
    if (!snapshot || !id || !snapshot->nodes->contains(*id) || snapshot->nodes->at(*id).kind != NodeKind::Layer)
        return false;
    if (snapshot->nodes->at(*id).visible == visible) return true;
    SetProperties properties;
    properties.id = *id; properties.visible = visible;
    const auto result = app_.apply(Command{properties});
    emit changed();
    return bool(result);
}
bool QtBridge::initializeCanvas(double w, double h, double ppi, bool whitePaper) {
    if(busy())return false;
    NewDocumentRequest request;
    request.dimensions = {w, h, Unit::Px, ppi};
    if(whitePaper){request.transparent=false;request.paperColor.channels={1,1,1,0};request.paperColor.alpha=1;}
    if (!app_.newDocument(request)) return false;
    const auto snapshot = app_.snapshot();
    if (!snapshot) return false;
    const auto objectId = uuid(), layerId = uuid();
    LayerNode object;
    object.id = objectId;
    object.parent = Document::rootId();
    object.kind = NodeKind::Object;
    object.name = "Artwork";
    object.bounds = {0, 0, snapshot->spec.pixelSize.width, snapshot->spec.pixelSize.height};
    LayerNode layer;
    layer.id = layerId;
    layer.parent = objectId;
    layer.objectId = objectId;
    layer.kind = NodeKind::Layer;
    layer.name = "Paint";
    layer.stage = "paint";
    layer.bounds = object.bounds;
    Command command;
    command.operations = {AddNode{object}, AddNode{layer}};
    const auto created = app_.apply(command);
    if (created) { selectedEditStroke_.reset();replayCount_=-1; activeLayer_ = layerId; ++canvasGeneration_; }
    return bool(created);
}
void QtBridge::createDocument(double w, double h, double ppi, bool whitePaper) {
    const bool created = initializeCanvas(w, h, ppi, whitePaper);
    if (created) {
        selectedBoundary_.reset();
        workflow_->reset();
        referenceUrl_ = QUrl{};
        referenceFeedback_.clear();
    } else {
        referenceFeedback_ = tr("Could not create the canvas. Check the dimensions.");
    }
    emit changed();
}
namespace {
QImage decodeOriginalReference(const QByteArray& bytes) {
 QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setAutoTransform(true);
 const auto fmt=reader.format();const auto size=reader.size();
 if((fmt!="png"&&fmt!="jpeg")||size.width()<=0||size.height()<=0||size.width()>8192||size.height()>8192||qint64(size.width())*size.height()>20000000)return {};
 auto image=reader.read();if(image.isNull()||image.width()>8192||image.height()>8192||qint64(image.width())*image.height()>20000000)return {};
 return image;
}
}
void QtBridge::refreshOriginalReference() const try {
 const auto s=app_.snapshot();const auto* map=s?s->resources.get():nullptr;if(map==referenceCacheResources_)return;
 const auto oldKey=referenceCacheKey_;const auto oldImage=referenceCacheImage_;const auto oldInfo=referenceCacheInfo_;
 referenceCacheResources_=map;referenceCacheOwner_=s?s->resources:nullptr;referenceCacheKey_.clear();referenceCacheInfo_.clear();referenceCacheImage_={};if(!s){referenceCacheBytes_.reset();return;}
 auto ref=originalReference(*s,false);if(!ref){referenceCacheInfo_=tr("原彩附件损坏，不能显示；未改动画作。");return;}if(!ref.value()){referenceCacheBytes_.reset();return;}
 const auto& r=*ref.value();const auto key=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.encoded.bytes.data()),32).toHex());
 if(key==oldKey&&s->resources->at(r.encoded).bytes==referenceCacheBytes_&&oldImage.width()==int(r.width)&&oldImage.height()==int(r.height)&&!oldImage.isNull()){referenceCacheKey_=oldKey;referenceCacheImage_=oldImage;referenceCacheInfo_=oldInfo;return;}
 if(!originalReference(*s)){referenceCacheInfo_=tr("原彩附件哈希不匹配；未改动画作。");return;}
 const auto& b=*s->resources->at(r.encoded).bytes;
 auto image=decodeOriginalReference(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
 if(image.isNull()||image.width()!=int(r.width)||image.height()!=int(r.height)){referenceCacheInfo_=tr("原彩附件尺寸/解码不匹配；未改动画作。");return;}
 const bool profile=image.colorSpace().isValid();if(profile)image.convertToColorSpace(QColorSpace::SRgb);
 if(image.isNull()){referenceCacheInfo_=tr("原彩屏幕色彩转换失败；工程字节仍保留。");return;}
 referenceCacheBytes_=s->resources->at(r.encoded).bytes;referenceCacheImage_=image;referenceCacheKey_=QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.encoded.bytes.data()),32).toHex());
 referenceCacheInfo_=tr("原彩参考 %1 × %2 px；显示缩放不增加细节。%3 原始文件字节随工程保存，导出画作不含参考；不是生成作品或作者证明。") .arg(r.width).arg(r.height).arg(profile?tr("屏幕按 sRGB 显示，非印刷校色。 "):tr("原图无已识别色彩配置，颜色未校准。 "));
}catch(const std::bad_alloc&){referenceCacheImage_={};referenceCacheKey_.clear();referenceCacheInfo_=tr("原彩显示内存不足；工程内容未因此修改。");}
QUrl QtBridge::originalReferenceUrl() const {refreshOriginalReference();return referenceCacheKey_.isEmpty()?QUrl{}:QUrl("image://original-reference/"+referenceCacheKey_);}
QString QtBridge::originalReferenceInfo() const {refreshOriginalReference();return referenceCacheInfo_;}
QImage QtBridge::originalReferenceImage(const QString& key) const {refreshOriginalReference();return key==referenceCacheKey_?referenceCacheImage_:QImage{};}
QVariantMap QtBridge::sampleOriginalReference(const QString& expectedUrl,double x,double y) const {
 const auto url=originalReferenceUrl();if(url.isEmpty()||expectedUrl!=url.toString()||!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=referenceCacheImage_.width()||y>=referenceCacheImage_.height())return {{"valid",false}};
 const int px=int(std::floor(x)),py=int(std::floor(y));auto color=referenceCacheImage_.pixelColor(px,py);const double alpha=color.alphaF();color.setAlpha(255);
 return {{"valid",true},{"color",color},{"hex",color.name()},{"alpha",alpha},{"x",px},{"y",py},{"source",url.toString()}};
}
bool QtBridge::attachOriginalReferenceUrl(const QUrl& url) try {
 const auto fail=[&](const QString& message){originalReferenceNotice_=message;emit changed();return false;};
 if(busy()||workflow_->pending()||replaying()||!app_.snapshot())return fail(tr("请先结束预览/回放并打开工程；原图未改变。"));
 if(!url.isLocalFile())return fail(tr("只接受本地 PNG/JPEG。"));
 QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>32*1024*1024)return fail(tr("原彩文件须在 32 MiB 以内。"));
 const auto expectedSize=file.size();const auto bytes=file.read(32*1024*1024+1);if(bytes.size()!=expectedSize||file.error()!=QFileDevice::NoError||bytes.isEmpty()||bytes.size()>32*1024*1024||!file.atEnd())return fail(tr("读取失败或超过预算。"));
 const auto image=decodeOriginalReference(bytes);if(image.isNull())return fail(tr("需可解码 PNG/JPEG，单边 ≤8192、总像素 ≤2000 万；不会偷偷缩小。"));
 Bytes encoded(reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size());
 const auto s=*app_.snapshot();auto existing=originalReference(s);if(!existing)return fail(tr("已有参考附件结构损坏，拒绝覆盖。"));
 if(existing.value()){if(existing.value()->encoded==sha256(encoded)){originalReferenceNotice_=tr("同一原彩已保存，没有重复导入。");emit changed();return true;}return fail(tr("本版每工程仅一份原彩；如刚导入错误可撤销，不会替换或覆盖。"));}
 auto command=attachOriginalReference(s,std::move(encoded),image.width(),image.height());if(!command||!app_.apply(command.value()))return fail(tr("保存原彩附件失败，画作未改变。"));
 originalReferenceNotice_=tr("已嵌入原始文件，请保存工程。分享 .draw 会同时分享这份原彩；PNG/ORA 画作不含它。");emit changed();return true;
}catch(const std::bad_alloc&){originalReferenceNotice_=tr("原彩导入内存不足，请核对附件状态；未缩小原图。");emit changed();return false;}
bool QtBridge::importReferenceUrl(const QUrl& url) {
    if(busy() || workflow_->pending())return false;
    if (!url.isLocalFile()) {
        referenceFeedback_ = tr("Only local image files are supported.");
        emit changed();
        return false;
    }
    const QFileInfo file(url.toLocalFile());
    const auto suffix = file.suffix().toLower();
    if (!file.isFile() || !file.isReadable() ||
        (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") &&
         suffix != QLatin1String("jpeg"))) {
        referenceFeedback_ = tr("Choose a readable PNG or JPEG image.");
        emit changed();
        return false;
    }
    const auto encodedSize = file.size();
    QImageReader reader(file.filePath());
    reader.setAutoTransform(true);
    const QSize imageSize = reader.size();
    const AlphaReferenceCandidate preliminary{
        true, true, encodedSize > 0 ? std::uint64_t(encodedSize) : 0,
        imageSize.width() > 0 ? std::uint32_t(imageSize.width()) : 0,
        imageSize.height() > 0 ? std::uint32_t(imageSize.height()) : 0};
    auto safe = validateAlphaReferenceCandidate(preliminary);
    if (!safe) {
        referenceFeedback_ = tr("Reference image must be PNG/JPEG, non-empty, <= 32 MB and <= 4096 px on each side.");
        emit changed();
        return false;
    }
    const QImage decoded = reader.read();
    safe = validateAlphaReferenceCandidate({true, !decoded.isNull(), preliminary.encodedBytes,
        decoded.width() > 0 ? std::uint32_t(decoded.width()) : 0,
        decoded.height() > 0 ? std::uint32_t(decoded.height()) : 0});
    if (!safe) {
        referenceFeedback_ = tr("Could not decode the image within the Alpha safety limits.");
        emit changed();
        return false;
    }
    const auto existing = app_.snapshot();
    const bool sameCanvas = existing &&
        existing->spec.pixelSize.width == std::uint64_t(decoded.width()) &&
        existing->spec.pixelSize.height == std::uint64_t(decoded.height());
    if (!sameCanvas && !initializeCanvas(decoded.width(), decoded.height(), 300.0)) {
        referenceFeedback_ = tr("Failed to create the reference canvas.");
        emit changed();
        return false;
    }
    referenceUrl_ = url;
    referenceFeedback_ = sameCanvas
        ? tr("Reference preview attached without changing existing strokes. The .draw file does not embed the image.")
        : tr("New canvas created from the image. The .draw file saves strokes, not the reference image.");
    emit changed();
    return true;
}
bool QtBridge::importLineartUrl(const QUrl& url) try {
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    auto s=app_.snapshot();
    if(!s || busy() || replaying() || workflow_->pending())return fail(tr("请先结束当前操作并回到完整工程。"));
    if(!s->eventIndex->empty() || !s->resources->empty())return fail(tr("仅向空白同尺寸工程嵌入线稿；现有作品未修改。"));
    const auto& children=s->nodes->at(Document::rootId()).children;
    if(!children.empty()){
        if(children.size()!=1 || s->nodes->size()!=3)return fail(tr("请使用新建的默认空白工程；现有图层结构保留不变。"));
        const auto& object=s->nodes->at(children.front());
        if(object.kind!=NodeKind::Object || object.name!="Artwork" || object.children.size()!=1)return fail(tr("现有自定义图层结构不会被覆盖。"));
        const auto unchanged=[](const LayerNode& n){return !n.locked && n.visible && n.opacity==1 && n.blend==BlendMode::Normal && n.maskId.empty() && n.transform==LayerTransform{} && n.content.empty();};
        if(!unchanged(object))return fail(tr("现有自定义图层属性不会被覆盖。"));
        const auto& layer=s->nodes->at(object.children.front());
        if(!unchanged(layer) || layer.name!="Paint" || layer.stage!="paint" || layer.kind!=NodeKind::Layer || !layer.content.empty())return fail(tr("现有自定义图层结构不会被覆盖。"));
    }
    if(s->spec.pixelSize.width>8192 || s->spec.pixelSize.height>8192 || s->spec.pixelSize.width*s->spec.pixelSize.height>20000000)return fail(tr("当前桌面填色上限为 2000 万像素、单边 8192；不会缩小图片。"));
    if(s->spec.colorSpec.format.mode!=ColorMode::RGB || s->spec.colorSpec.format.depth!=Depth::U8 || !s->spec.colorSpec.profileId.empty())return fail(tr("当前线稿导入仅支持未指定 ICC 的 RGB 8 位工程。"));
    if(!url.isLocalFile())return fail(tr("请选择本地 PNG 线稿。"));
    QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly) || file.size()<=0 || file.size()>32*1024*1024)return fail(tr("PNG 文件须可读且不超过 32 MiB。"));
    const auto expectedSize=file.size();const auto encoded=file.read(32*1024*1024+1);
    if(encoded.size()!=expectedSize || !file.atEnd())return fail(tr("读取 PNG 时文件发生变化或读取不完整，未导入。"));
    QBuffer buffer;buffer.setData(encoded);if(!buffer.open(QIODevice::ReadOnly))return fail(tr("无法读取 PNG 缓冲。"));
    QImageReader reader(&buffer,"png");reader.setAutoTransform(false);const auto size=reader.size();
    if(size.width()!=int(s->spec.pixelSize.width) || size.height()!=int(s->spec.pixelSize.height))return fail(tr("线稿必须与画布像素尺寸一致；不会自动放大或缩小。"));
    auto image=reader.read();if(image.isNull())return fail(tr("PNG 解码失败，工程未修改。"));
    if(image.colorSpace().isValid())return fail(tr("带 ICC/色彩空间标签的线稿尚未接通转换，未修改工程。"));
    image=image.convertToFormat(QImage::Format_RGBA8888);
    if(image.isNull() || image.size()!=size)return fail(tr("线稿像素转换失败，工程未修改。"));
    Bytes rgba(std::size_t(image.width())*image.height()*4);
    for(int y=0;y<image.height();y++)std::copy_n(image.constScanLine(y),std::size_t(image.width())*4,rgba.begin()+std::size_t(y)*image.width()*4);
    for(std::size_t i=0;i<rgba.size();i+=4)if(rgba[i+3] && (rgba[i]!=rgba[i+1] || rgba[i+1]!=rgba[i+2]))return fail(tr("此入口只接受黑白灰线稿，不会把彩色成图自动转成线稿。"));
    const auto ink=uuid(),fill=uuid();auto import=makeLineartRasterCommand(std::move(rgba),Bytes(encoded.begin(),encoded.end()),image.width(),image.height(),ink);if(!import)return fail(QString::fromStdString(import.error().messageKey));
    Command command;for(auto id:s->nodes->at(Document::rootId()).children)command.operations.push_back(RemoveNode{id});
    LayerNode layer;layer.id=fill;layer.parent=Document::rootId();layer.name="Color fill";layer.bounds={0,0,s->spec.pixelSize.width,s->spec.pixelSize.height};command.operations.push_back(AddNode{layer});
    command.operations.insert(command.operations.end(),import.value().operations.begin(),import.value().operations.end());
    auto candidate=Document::fromSnapshot(*s);if(!candidate)return fail(QString::fromStdString(candidate.error().messageKey));auto applied=candidate.value().apply(command,s->revision);if(!applied)return fail(QString::fromStdString(applied.error().messageKey));auto plan=planMergedDocument(candidate.value().snapshot());if(!plan)return fail(QString::fromStdString(plan.error().messageKey));
    auto result=app_.apply(command);if(!result)return fail(QString::fromStdString(result.error().messageKey));
    selectedEditStroke_.reset();activeLayer_=fill;++canvasGeneration_;referenceUrl_=QUrl{};fillNotice_=tr("原 PNG 已嵌入并锁定；色块写入独立底层。仅按原线稿取样，不重绘、不自动补洞。");emit changed();return true;
}catch(const std::bad_alloc&){fillNotice_=tr("内存不足，线稿未导入。");emit changed();return false;}
QString QtBridge::selectedBoundaryName() const {
 if(!selectedBoundary_)return {};
 const auto s=app_.snapshot();
 if(!s||!s->nodes->contains(*selectedBoundary_))return tr("所选边界层已不可用，请清除或重选");
 return QString::fromStdString(s->nodes->at(*selectedBoundary_).name);
}
bool QtBridge::useActiveBoundary(){
 if(busy()||replaying()||workflow_->pending())return false;
 const auto s=app_.snapshot();if(!s||!activeLayer_||!s->nodes->contains(*activeLayer_)||s->nodes->at(*activeLayer_).stage!=FinishingLayerStage||!layerVisible(*s,*activeLayer_)){fillNotice_=tr("请明确选择一个可见手绘细节层作为边界；不会自动采样其他颜色层。");emit changed();return false;}
 selectedBoundary_=activeLayer_;fillNotice_=tr("已选择该手绘层的 alpha≥128/255 覆盖作为临时边界；不改颜色/透明度，只限原稿白区填色，先看预览。重开后须重选。");emit changed();return true;
}
bool QtBridge::clearSelectedBoundary(){if(busy()||workflow_->pending())return false;selectedBoundary_.reset();fillNotice_=tr("已停止采样手绘边界，恢复原线稿/明确补线取样。");emit changed();return true;}
bool QtBridge::fillLineartAt(double x,double y,const QColor& color,bool hollow,std::optional<QRectF> limit,std::vector<DocumentPoint> polygon,std::optional<QPointF> gapEnd) try {
    auto s=app_.snapshot();
    const auto fail=[&](const QString& reason){fillNotice_=reason;emit changed();return false;};
    if(!s || busy() || replaying() || workflow_->pending() || !activeLayer_ || !color.isValid() || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=double(s->spec.pixelSize.width) || y>=double(s->spec.pixelSize.height))return fail(tr("填色未执行：请选可编辑色层并结束当前操作。"));
    if(gapEnd){const double coords[]={x,y,gapEnd->x(),gapEnd->y()};for(auto v:coords)if(!std::isfinite(v)||v<0||std::floor(v)!=v)return fail(tr("补线端点须为有效原图整像素；未修改工程。"));if(hollow||limit||!polygon.empty()||gapEnd->x()>=s->spec.pixelSize.width||gapEnd->y()>=s->spec.pixelSize.height)return fail(tr("补线端点越界或工具冲突；未修改工程。"));}
    if(!polygon.empty()&&(!hollow||limit))return fail(tr("套索与矩形限制不能同时应用；工程未修改。"));
    std::optional<Rect> bounds;
    if(limit){
        const auto r=*limit;const double values[]={r.x(),r.y(),r.width(),r.height()};
        for(const auto v:values)if(!std::isfinite(v)||v<0||std::floor(v)!=v)return fail(tr("矩形范围须为非负整数原图像素；未执行挖空。"));
        if(!hollow||r.width()<1||r.height()<1||r.right()>double(s->spec.pixelSize.width)||r.bottom()>double(s->spec.pixelSize.height)||x<r.left()||y<r.top()||x>=r.right()||y>=r.bottom())return fail(tr("矩形必须在画布内，并点击矩形内的墨块；未修改工程。"));
        bounds=Rect{std::int64_t(r.x()),std::int64_t(r.y()),std::uint64_t(r.width()),std::uint64_t(r.height())};
    }
    NodeId source;for(const auto&[id,node]:*s->nodes)if(node.stage==LineartStage){if(!source.empty())return fail(tr("目前只支持一个原线稿图层。"));source=id;}
    if(hollow && s->spec.background.transparent)return fail(tr("局部挖空是白纸覆盖；请新建时选择白纸。透明工程未修改。"));
    ColorValue paint;paint.activeChannels=3;paint.profileId=s->spec.colorSpec.profileId;paint.channels={color.redF(),color.greenF(),color.blueF(),0};paint.alpha=color.alphaF();
    fillPool_.setMaxThreadCount(1);fillStop_=std::stop_source{};const auto stop=fillStop_.get_token();
    const auto snapshot=*s;auto target=*activeLayer_;NodeId hollowOverlay;bool derived=false;
    auto ordered=s->nodes->at(Document::rootId()).children;bool withDetails=false;while(!ordered.empty()&&s->nodes->at(ordered.back()).stage==FinishingLayerStage){ordered.pop_back();withDetails=true;}
    if(withDetails&&!hollow&&!gapEnd&&!ordered.empty())target=ordered.front();
    if((ordered.size()==3||ordered.size()==4) && ordered[1]==source){hollowOverlay=ordered[2];if(!hollow&&!gapEnd){derived=true;target=ordered.size()==4?ordered[3]:uuid();}}
    const auto event=uuid();const auto generation=canvasGeneration_;
    const auto boundary=(!hollow&&!gapEnd)?selectedBoundary_:std::optional<NodeId>{};
    fillBusy_=true;fillNotice_=boundary?tr("正在计算原稿白区与明确所选手绘层的边界；alpha≥128/255，其他细节不取样，可取消。请核对预览。"):tr("正在按原尺寸线稿计算区域；手绘细节不参与边界，尚未提交，可取消。请接受预览后再保存。");emit changed();
    fillPool_.start([this,snapshot,source,target,event,x,y,paint,stop,generation,hollow,derived,hollowOverlay,bounds,polygon=std::move(polygon),gapEnd,boundary]{
        Result<Command> result=error(ErrorCode::InvalidInput,"hollow.selection");
        std::uint64_t selectedInk=0;
        try{
            auto projected=projectControlledLineart(snapshot,stop);
            if(!projected)result=projected.error();else {
            const auto& controlled=projected.value().controlled;
            if(boundary)result=selectedBoundaryFillCommand(snapshot,*boundary,event,std::uint32_t(x),std::uint32_t(y),paint,stop);
            else if(gapEnd)result=closeLineartGapCommand(controlled,source,hollowOverlay.empty()?uuid():hollowOverlay,event,std::uint32_t(x),std::uint32_t(y),std::uint32_t(gapEnd->x()),std::uint32_t(gapEnd->y()),stop);
            else if(derived)result=derivedHollowFillCommand(controlled,source,hollowOverlay,target,event,std::uint32_t(x),std::uint32_t(y),paint,stop);
            else if(!hollow)result=originalLineartFillCommand(controlled,source,target,event,std::uint32_t(x),std::uint32_t(y),paint,stop);
            else{
                const auto w=controlled.spec.pixelSize.width,h=controlled.spec.pixelSize.height;auto raw=lineartRasterPixels(controlled,source);
                if(!raw)result=raw.error();else if(w>8192||h>8192||w*h>20000000)result=error(ErrorCode::BudgetExceeded,"hollow.canvas-limit");else{
                    std::vector<Rgba8> pixels(std::size_t(w*h));bool cancelled=false;
                    for(std::size_t i=0;i<pixels.size();i++){if(i%16384==0&&stop.stop_requested()){cancelled=true;break;}const auto p=raw.value().subspan(i*4,4);const auto v=std::uint8_t(p[3]&&p[0]<255?0:255);pixels[i]={v,v,v,255};}
                    if(cancelled)result=error(ErrorCode::Cancelled,"hollow.cancelled");
                    else if(pixels[std::size_t(y)*w+std::size_t(x)].r!=0)result=error(ErrorCode::InvalidInput,"hollow.click-ink-not-paper");
                    else{auto selection=floodFill(pixels,std::uint32_t(w),std::uint32_t(h),std::uint32_t(x),std::uint32_t(y),0,{},stop);if(selection&&bounds)selection=clipFillSelection(selection.value(),std::uint32_t(w),std::uint32_t(h),*bounds,stop);if(selection&&!polygon.empty()){selection=clipPolygonSelection(selection.value(),std::uint32_t(w),std::uint32_t(h),polygon,stop);if(selection){bool inside=false;for(const auto& run:selection.value().runs)if(run.y==std::uint32_t(y)&&std::uint32_t(x)>=run.x&&std::uint32_t(x)<run.x+run.length){inside=true;break;}if(!inside)selection=error(ErrorCode::InvalidInput,"clip-polygon.seed-outside");}}if(!selection)result=selection.error();else{selectedInk=selection.value().pixelCount;std::vector<Rgba8>().swap(pixels);result=hollowInkOverlayCommand(controlled,source,hollowOverlay.empty()?event:hollowOverlay,uuid(),selection.value(),1,stop);}}
                }
            }
            if(result&&!boundary)result=restoreControlledCommand(snapshot,projected.value(),std::move(result.value()),stop);
            }
        }catch(const std::bad_alloc&){result=error(ErrorCode::BudgetExceeded,"hollow.selection-allocation");}
        std::optional<DocumentSnapshot> preview;FillRegionReview review;
        try { if(result){
            FillMask mask;for(const auto& op:result.value().operations)if(const auto* paint=std::get_if<Paint>(&op))for(std::size_t i=0;i+1<paint->event.geometry.size();i+=2){const auto& a=paint->event.geometry[i];const auto& b=paint->event.geometry[i+1];const auto length=std::uint32_t(b.x-a.x);mask.runs.push_back({std::uint32_t(a.y),std::uint32_t(a.x),length});mask.pixelCount+=length;}
            auto inspected=reviewFillRegion(mask,std::uint32_t(snapshot.spec.pixelSize.width),std::uint32_t(snapshot.spec.pixelSize.height),stop);
            if(!inspected)result=inspected.error();else{
                review=inspected.value();auto candidate=Document::fromSnapshot(snapshot);
                if(!candidate)result=candidate.error();else{auto applied=candidate.value().apply(result.value(),snapshot.revision);if(!applied)result=applied.error();else preview=candidate.value().snapshot();}
            }
        }
        }catch(const std::bad_alloc&){preview.reset();result=error(ErrorCode::BudgetExceeded,"fill-review.allocation");}
        QMetaObject::invokeMethod(this,[this,snapshot,result=std::move(result),preview,review,stop,generation,hollow,selectedInk,derived,gapEnd,boundary]() mutable {
            const auto current=app_.snapshot();fillBusy_=false;
            if(stop.stop_requested()){fillNotice_=tr("区域操作已取消，工程未修改。");emit changed();return;}
            if(replaying() || !current || current->documentId!=snapshot.documentId || current->revision!=snapshot.revision || canvasGeneration_!=generation){fillNotice_=tr("原工程已变化，旧填色结果已丢弃。");emit changed();return;}
            if(!result){
                if(hollow && result.error().messageKey=="hollow.no-safe-interior")fillNotice_=tr("此处是细笔触或没有可安全挖空的内部，已保留，不会扩大选区。");
                else if(hollow && result.error().messageKey=="hollow.already-cleared")fillNotice_=tr("所选内部已经挖空，未重复覆盖；请点击另一个独立墨块。原稿和已填颜色未改。");
                else fillNotice_=tr("区域操作未执行，工程未修改：")+QString::fromStdString(result.error().messageKey);
                emit changed();return;
            }
            pendingFill_=std::move(result.value());pendingFillSource_=snapshot;pendingFillPreview_=preview;pendingFillGeneration_=generation;
            fillNotice_=tr("未提交预览：%1 像素；范围 %2×%3；%4。内部建议点 (%5,%6)，离区域外像素中心的曼哈顿距离 %7。%8请对照画面后接受或取消。")
                .arg(qulonglong(review.pixelCount)).arg(qlonglong(review.bounds.width)).arg(qlonglong(review.bounds.height))
                .arg(review.touchesCanvasEdge?tr("连到画布边缘，警惕背景漏选"):tr("未连画布边缘"))
                .arg(review.seedX).arg(review.seedY).arg(review.clearance).arg(review.tiny?tr("注意：极小色块，可能点中了碎岛。 "):QString{});
            if(derived&&!boundary){bool lower=false;for(const auto& op:pendingFill_->operations)if(const auto* paint=std::get_if<Paint>(&op))lower=paint->event.layerId==snapshot.nodes->at(Document::rootId()).children.front();
                fillNotice_=(lower?tr("原线稿白区填色预览：接受后写入下方色层；挖空区域和上方颜色不变。参考原图及已接受补线，不采已填颜色。"):tr("挖空内部填色预览：接受后写入上方色层；原线稿白区和下方颜色不变。仅取样原图加已接受覆盖。"))+"\n"+fillNotice_;}

            if(boundary)fillNotice_=tr("明确所选手绘层的 alpha≥128/255 与原稿白区共同取样；只写下方色层，笔迹透明度/原稿未改。\n")+fillNotice_;
            if(hollow)fillNotice_=tr("局部挖空未提交：选中墨块 %1 像素，仅清空安全内部 %2 像素。原轮廓、细笔触及其他区域保留；白纸覆盖，不是透明擦除。请对照预览后接受或取消。").arg(qulonglong(selectedInk)).arg(qulonglong(review.pixelCount));
            if(gapEnd)fillNotice_=tr("短断口补线未提交：%1 像素，黑色1像素连接。仅连接指定端点，不改原图；接受后填色才会参考此线。请确认没有封错位置。").arg(qulonglong(review.pixelCount));
            emit changed();
        },Qt::QueuedConnection);
    });
    return true;
}catch(const std::bad_alloc&){fillStop_.request_stop();fillBusy_=false;fillNotice_=tr("填色任务启动内存不足，未修改工程。");emit changed();return false;}
Result<void> QtBridge::commitStroke(const StrokeEvent& event) {
    const auto snapshot = app_.snapshot();
    if (!snapshot || busy() || replaying() || !nativeLayerPaintable(*snapshot, event.layerId))
        return error(ErrorCode::InvalidInput, "paint.layer-unavailable");
    const auto bounds = strokeBounds(event);
    if (!bounds) return bounds.error();
    const auto committed = app_.apply(Command{Paint{event, bounds.value()}});
    if (!committed) return committed.error();
    emit changed();
    return {};
}
void QtBridge::openUrl(const QUrl& p){
    if(busy()){fillNotice_=tr("请等待或取消当前任务后再打开工程。");emit changed();return;}
    if(!p.isLocalFile())return;
    auto done=app_.open(std::filesystem::path(p.toLocalFile().toStdWString()));
    if(done){selectedBoundary_.reset();replayCount_=-1;referenceUrl_=QUrl{};referenceFeedback_=tr("Reference previews are not stored in .draw projects. Reimport the source image to compare.");++canvasGeneration_;resetLayerSelection();workflow_->restore(*app_.snapshot());}
    emit changed();
}
void QtBridge::saveUrl(const QUrl& p){if(cubicDraftActive()){fillNotice_=tr("曲线草稿尚未接受，本次未保存；请预览并接受，或明确取消草稿。");emit changed();return;}if(strokeCommitBusy_){fillNotice_=tr("笔迹仍在验收；请等待完成或取消后再保存。");emit changed();return;}if(fillBusy_ || fillPending()){fillNotice_=tr("填色尚未提交，本次未保存；请等待完成或取消后重试。");emit changed();return;}if(!p.isLocalFile())return;if(workflow_->pending()){workflow_->fail(tr("请先接受审阅蒙版，再保存工程。未接受的修改仍然保留；也可取消关闭或明确放弃。" ));return;}auto done=app_.save(std::filesystem::path(p.toLocalFile().toStdWString()));(void)done;emit changed();}
void QtBridge::probeWorker(){if(busy()||!app_.snapshot())return;auto s=*app_.snapshot();auto bytes=encodeDelta(DocumentSnapshot{},s);auto hash=stateHash(s);if(!bytes||!hash)return;JobRequest q;q.id=uuid();q.nonce=uuid();q.task="native.smoke";q.inputSnapshot={s.documentId,s.revision,hash.value()};q.snapshotBytes=std::move(bytes.value());q.resources={q.id,ResourceDevice::CPU,16*1024*1024,0,0,false};auto id=broker_.submit(q);if(id){running_=id.value();workerMessage_=tr("Connecting native worker...");}else workerMessage_=QString::fromStdString(id.error().messageKey);emit changed();}
void QtBridge::cancelWorker(){if(workflow_->busy())workflow_->cancel();if(running_){broker_.cancel(*running_);workerMessage_=tr("Cancelling worker...");emit changed();}}
}

