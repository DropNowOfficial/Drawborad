#include "WorkflowController.h"
#include "ReviewJob.h"
#include "ReconstructionJob.h"
#include "PaintedDocument.h"
#include "SemanticManifest.h"
#include "codecs/ImageReader.h"
#include "apps/studio/QtBridge.h"
#include "storage/ProjectCodec.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
namespace drawborad {
namespace {
Id uuid(){const auto bytes=QUuid::createUuid().toRfc4122();Id id;std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()),16,id.bytes.begin());return id;}
std::optional<Hash> parseHash(const QString& text){
    const auto bytes=QByteArray::fromHex(text.toLatin1());
    if(text.size()!=64 || bytes.size()!=32 || QString::fromLatin1(bytes.toHex())!=text.toLower()) return {};
    Hash hash;std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.constData()),32,hash.bytes.begin());return hash;
}
QUrl pinSource(const QJsonObject& metadata,const QString& directory){
    if(directory.isEmpty() || !QDir(directory).exists())return {};
    QFile source(metadata.value("image").toString());
    if(source.size()<=0 || source.size()>32*1024*1024 || !source.open(QIODevice::ReadOnly))return {};
    const auto bytes=source.readAll();
    const auto hash=sha256(std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),std::size_t(bytes.size())));
    if(hash.hex()!=metadata.value("sourceHash").toString().toStdString())return {};
    const auto suffix=QFileInfo(source).suffix().toLower()=="png"?".png":".jpg";
    const auto path=QDir(directory).filePath("source-preview-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+suffix);
    QSaveFile preview(path);
    if(!preview.open(QIODevice::WriteOnly) || preview.write(bytes)!=bytes.size() || !preview.commit())return {};
    return QUrl::fromLocalFile(path);
}
std::optional<QJsonObject> associatedSource(const DocumentSnapshot& snapshot){
    for(const auto& [id,node]:*snapshot.nodes){
        (void)id;if(node.kind!=NodeKind::Object || node.maskId.empty())continue;
        const auto mask=snapshot.resources->find(node.maskId);if(mask==snapshot.resources->end())continue;
        for(const auto dependency:mask->second.dependencies){
            const auto resource=snapshot.resources->find(dependency);if(resource==snapshot.resources->end() || resource->second.kind!=ResourceKind::Reference || resource->second.bytes->size()>1024*1024)continue;
            const auto& b=*resource->second.bytes;const auto json=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
            if(json.isObject() && json.object().value("type").toString()==QLatin1String(SourceReferenceType))return json.object();
        }
    }
    return {};
}
}
WorkflowController::WorkflowController(QtBridge& bridge)
    :QObject(&bridge),bridge_(bridge),
     host_(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString())/L"DrawboradWorker.exe"),
     broker_(host_,governor_,clock_),directory_(std::make_unique<QTemporaryDir>()) {
    const auto package=qEnvironmentVariable("DRAWBORAD_MODEL_PACKAGE");
    if(!package.isEmpty()){modelPackage_=QUrl::fromLocalFile(package);modelState_=QStringLiteral("NotChecked");}
    modelStatus_=package.isEmpty()?tr("请选择本地模型包。"):tr("已选择模型包；尚未检查 CPU 推理。");
    poll_.setInterval(25);connect(&poll_,&QTimer::timeout,this,&WorkflowController::poll);poll_.start();
}
WorkflowController::~WorkflowController(){
    if(semanticProcess_){semanticProcess_->disconnect(this);semanticProcess_->kill();semanticProcess_->waitForFinished(3000);}
    if(running_) broker_.workerFailed(running_->id);
}
void WorkflowController::fail(const QString& text){message_=text;emit changed();}
void WorkflowController::setModelPackage(const QUrl& url){
    if(busy() || bridge_.running_ || !url.isLocalFile()) return;
    modelPackage_=url;modelReceipt_.reset();modelState_=QStringLiteral("NotChecked");
    modelStatus_=tr("已选择模型包；请检查哈希、接口与真实 CPU 推理。");emit changed();
}
void WorkflowController::stopModelCheck(const QString& reason){
    if(!running_ || running_->task!="model.preflight.v1")return;
    modelReceipt_.reset();modelState_=QStringLiteral("NotChecked");modelStatus_=reason;
}
bool WorkflowController::checkModel(){
    if(busy() || bridge_.running_ || !modelPackage_.isLocalFile())return false;
    NewDocumentRequest request;request.dimensions={1,1,Unit::Px,300};
    const auto spec=validateDocument(request,{});if(!spec)return false;
    auto document=Document::create(spec.value(),uuid());if(!document)return false;
    auto directory=std::make_unique<QTemporaryDir>();if(!directory->isValid())return false;
    const auto metadata=jsonResource({{"type",ModelPreflightRequestType},{"package",modelPackage_.toLocalFile()},{"outputDirectory",directory->path()}});
    if(!document.value().apply(Command{InstallResource{metadata}},document.value().snapshot().revision))return false;
    const auto snapshot=document.value().snapshot();auto bytes=encodeDelta({},snapshot);auto hash=stateHash(snapshot);
    if(!bytes || !hash)return false;
    JobRequest job;job.id=uuid();job.nonce=uuid();job.task="model.preflight.v1";
    job.inputSnapshot={snapshot.documentId,snapshot.revision,hash.value()};job.snapshotBytes=std::move(bytes.value());
    job.budget.ramSoftBytes=2*GiB;job.budget.ramReclaimBytes=3*GiB;job.resources={job.id,ResourceDevice::CPU,2*GiB,0,0,false};
    modelReceipt_.reset();modelState_=QStringLiteral("CheckingModel");progressCompleted_=0;progressTotal_=0;
    const auto submitted=broker_.submit(job);
    if(!submitted){modelState_=QStringLiteral("CheckFailed");modelStatus_=QString::fromStdString(submitted.error().messageKey);emit changed();return false;}
    analysis_=std::move(document.value());jobDirectory_=std::move(directory);running_=std::move(job);
    modelStatus_=tr("后台校验本地模型并执行 CPU 编码器/解码器冒烟推理，可取消。");
    message_=tr("模型检查不会改变当前绘画或审阅。");emit changed();return true;
}
bool WorkflowController::loadModelResult(const DocumentSnapshot& snapshot){
    const auto result=findJsonResource(snapshot,ModelPreflightResultType);if(!result)return false;
    const auto state=result->value("state").toString();
    const QStringList states{QStringLiteral("MissingModel"),QStringLiteral("InvalidManifest"),QStringLiteral("ModelIntegrityFailed"),
        QStringLiteral("RuntimeUnavailable"),QStringLiteral("ProviderUnavailable"),QStringLiteral("IncompatibleModel"),QStringLiteral("Ready")};
    if(!states.contains(state) || result->value("package").toString()!=modelPackage_.toLocalFile() ||
       result->value("provider").toString()!="CPU" || result->value("receiptVersion").toInt()!=1)return false;
    if(state=="Ready"){
        if(result->value("probeKind").toString()!="deterministic-smoke" ||
           !parseHash(result->value("manifestHash").toString()) || !parseHash(result->value("encoderHash").toString()) ||
           !parseHash(result->value("decoderHash").toString()))return false;
        modelReceipt_=*result;
        modelStatus_=tr("CPU 编码器和解码器冒烟推理通过（%1 ms）；图像质量仍需审阅。").arg(result->value("elapsedUs").toString().toULongLong()/1000);
    }else{
        modelReceipt_.reset();
        const auto reason=result->value("messageKey").toString();
        if(state=="MissingModel")modelStatus_=tr("模型包或所需模型文件不存在：%1").arg(reason);
        else if(state=="InvalidManifest")modelStatus_=tr("模型清单无效：%1").arg(reason);
        else if(state=="ModelIntegrityFailed")modelStatus_=tr("模型文件哈希校验失败：%1").arg(reason);
        else if(state=="RuntimeUnavailable")modelStatus_=tr("本地 ONNX Runtime 不可用：%1").arg(reason);
        else if(state=="ProviderUnavailable")modelStatus_=tr("所选执行设备不可用：%1").arg(reason);
        else modelStatus_=tr("模型接口或推理输出不兼容：%1").arg(reason);
        const auto diagnostic=result->value("diagnostic").toString();if(!diagnostic.isEmpty())modelStatus_+=QStringLiteral(" · ")+diagnostic;
    }
    modelState_=state;message_=tr("模型检查完成；当前绘画和审阅未改变。");return true;
}
QVariantList WorkflowController::candidates() const {
    QVariantList rows;
    for(std::size_t i=0;i<names_.size();++i){
        const auto label=background_[i]?tr("人工检查的剩余背景候选"):std::isfinite(scores_[i])?tr("模型 IoU 预测 %1").arg(scores_[i],0,'f',3):tr("已有对象蒙版");
        rows.push_back(QVariantMap{{"index",int(i)},{"name",names_[i]},{"included",selected_[i]},
            {"score",label},{"existing",!objects_[i].empty()}});
    }
    return rows;
}
QVariantList WorkflowController::promptPoints() const{
    QVariantList result;for(const auto& p:promptPoints_)result.push_back(QVariantMap{{"x",p.x},{"y",p.y},{"positive",p.positive}});return result;
}
QVariantList WorkflowController::promptBox() const{
    if(!promptBox_)return {};return {promptBox_->x0,promptBox_->y0,promptBox_->x1,promptBox_->y1};
}
bool WorkflowController::addPromptPoint(double x,double y,bool positive){
    if(busy() || !review_ || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=imageWidth() || y>=imageHeight() || promptPoints_.size()>=64)return false;
    promptPoints_.push_back({x,y,positive});message_=tr("已设置 %1 个点提示；红色排除、绿色包含。设置提示框后可请求新候选。" ).arg(promptPoints_.size());emit changed();return true;
}
bool WorkflowController::setPromptBox(double x0,double y0,double x1,double y1){
    if(busy() || !review_)return false;for(const auto value:{x0,y0,x1,y1})if(!std::isfinite(value))return false;
    const auto left=std::clamp(std::min(x0,x1),0.0,double(imageWidth())),top=std::clamp(std::min(y0,y1),0.0,double(imageHeight()));
    const auto right=std::clamp(std::max(x0,x1),0.0,double(imageWidth())),bottom=std::clamp(std::max(y0,y1),0.0,double(imageHeight()));
    if(right<=left || bottom<=top)return false;promptBox_=PromptBox{left,top,right,bottom};
    message_=tr("已设置提示框 (%1,%2)–(%3,%4)，现有候选和笔迹保持不变。" ).arg(left).arg(top).arg(right).arg(bottom);emit changed();return true;
}
void WorkflowController::clearPrompts(){if(busy())return;promptPoints_.clear();promptBox_.reset();emit changed();}
bool WorkflowController::startPrompted(){
    if(!review_ || !sourceMetadata_ || sourceUrl_.isEmpty() || (promptPoints_.empty() && !promptBox_))return false;
    return startAnalysis(sourceUrl_,true);
}
bool WorkflowController::subtractIncluded(){
    if(busy() || !review_ || activeIndex_<0 || !selected_[std::size_t(activeIndex_)] || std::count(selected_.begin(),selected_.end(),true)!=2)return false;
    if(!canRecordEdit())return false;
    std::size_t other=0;while(!selected_[other] || other==std::size_t(activeIndex_))++other;
    MaskEdit edit;edit.operation=MaskEditOp::SubtractMask;edit.objectIndex=std::size_t(activeIndex_);edit.secondaryIndex=other;edit.expectedRevision=review_->revision();
    const auto before=uiState();const auto done=review_->apply(edit);if(!done){fail(QString::fromStdString(done.error().messageKey));return false;}
    undoUi_.push_back(before);reviewAudit_.append(QJsonObject{{"operation","subtract-visible-overlap"},{"target",activeIndex_},{"other",qint64(other)}});
    pending_=true;updateOverlay();emit changed();return true;
}
bool WorkflowController::addBackgroundCandidate(){
    if(busy() || !review_ || sourceUrl_.isEmpty() || !sourceMetadata_ || !canRecordEdit())return false;
    std::vector<std::size_t> indices;
    for(std::size_t i=0;i<names_.size();++i){
        if(!objects_[i].empty() && !selected_[i]){fail(tr("确认背景前请勾选全部已有对象；未勾选的已有对象会保持原样。"));return false;}
        if(selected_[i])indices.push_back(i);
    }
    const auto json=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(sourceMetadata_->bytes->data()),qsizetype(sourceMetadata_->bytes->size()))).object();
    const auto image=decodeImage(std::filesystem::path(sourceUrl_.toLocalFile().toStdWString()),{});
    if(!image || !image.value().decodedPixels || image.value().info.sourceHash.hex()!=json.value("sourceHash").toString().toStdString())return false;
    const auto& tile=*image.value().decodedPixels;Bytes alpha;
    if(tile.width!=unsigned(imageWidth()) || tile.height!=unsigned(imageHeight()) || tile.format.mode!=ColorMode::RGB || tile.format.depth!=Depth::U8)return false;
    if(tile.format.alpha){alpha.resize(std::size_t(tile.width)*tile.height);for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x)alpha[std::size_t(y)*tile.width+x]=tile.bytes[std::size_t(y*tile.rowStride+x*4+3)];}
    const auto before=uiState();const auto done=review_->addBackground(indices,alpha,review_->revision());if(!done){fail(QString::fromStdString(done.error().messageKey));return false;}
    undoUi_.push_back(before);names_.insert(names_.begin(),tr("背景（检查剩余可见区域）"));scores_.insert(scores_.begin(),std::numeric_limits<double>::quiet_NaN());selected_.insert(selected_.begin(),false);
    objects_.insert(objects_.begin(),NodeId{});baseMasks_.insert(baseMasks_.begin(),Hash{});background_.insert(background_.begin(),true);activeIndex_=0;
    reviewAudit_.append(QJsonObject{{"operation","explicit-remaining-background-proposal"},{"sourceAlphaPreserved",true}});
    pending_=true;message_=tr("背景候选已生成但尚未接受。请检查它是否误含未识别的前景；需要时先修正对象，再明确勾选。" );updateOverlay();emit changed();return true;
}

bool WorkflowController::importSemanticManifest(const QUrl& url) {
    if(busy() || bridge_.running_)return false;
    const auto parsed=loadSemanticMaskManifest(url);
    if(!parsed){fail(tr("AI 分层工程校验失败：%1").arg(QString::fromStdString(parsed.error().messageKey)));return false;}
    // No partially-installed candidate state on a bad manifest.
    auto review=ReviewSession::create(parsed.value().proposals,parsed.value().masks,0);
    if(!review){fail(QString::fromStdString(review.error().messageKey));return false;}
    const auto pinned=pinSource(parsed.value().sourceReference,directory_->path());
    if(pinned.isEmpty()){fail(tr("原图丢失或哈希变化；分层没有被导入。"));return false;}
    const auto current=bridge_.canvasSnapshot();
    sourceMetadata_=jsonResource(parsed.value().sourceReference);
    sourceUrl_=pinned;
    review_=std::move(review.value());
    names_=parsed.value().names;
    scores_=parsed.value().scores;
    background_=parsed.value().background;
    selected_.assign(names_.size(),true);
    objects_.assign(names_.size(),{});
    baseMasks_.assign(names_.size(),{});
    undoUi_.clear();
    activeIndex_=0;
    pending_=true;
    baseDocument_=current?std::optional<DocumentId>{current->documentId}:std::nullopt;
    baseRevision_=current?current->revision:0;
    promptPoints_.clear();promptBox_.reset();
    reviewAudit_=QJsonArray{QJsonObject{{"operation","sam3-semantic-proposals"},
        {"backend",parsed.value().backend},{"candidates",int(names_.size())},
        {"humanQualityApproval",false}}};
    updateOverlay();
    message_=tr("已载入 %1 个带名称的独立对象层。请检查后点击“接受对象”，再执行“生成笔迹”。").arg(names_.size());
    emit changed();return true;
}
void WorkflowController::setSam3Checkpoint(const QUrl& url) {
    if(busy() || !url.isLocalFile())return;
    const QFileInfo info(url.toLocalFile());
    if(!info.isFile() || info.size()<=0 || info.suffix().toLower()!=QLatin1String("pt")){
        fail(tr("请选择本地 SAM 3 图像模型 .pt 权重。"));return;
    }
    sam3Checkpoint_=url;
    message_=tr("已选择本地 SAM 3 图像模型。推理时按图像内容逐对象检测，不会上传图片。");
    emit changed();
}
bool WorkflowController::startSam3Image(const QUrl& url,const QString& promptList) {
    if(busy() || bridge_.running_ || !url.isLocalFile())return false;
    const QFileInfo image(url.toLocalFile());
    if(!image.isFile() || image.size()<=0 || image.size()>32*1024*1024){
        fail(tr("请选择小于 32MB 的本地 PNG/JPEG 图像。"));return false;
    }
    auto prompts=promptList.split(QRegularExpression(QStringLiteral("[,，;；\\n]+")),Qt::SkipEmptyParts);
    for(auto& prompt:prompts)prompt=prompt.trimmed();
    if(prompts.isEmpty() || prompts.size()>63 ||
       std::any_of(prompts.begin(),prompts.end(),[](const QString& p){return p.isEmpty() || p.size()>128;})){
        fail(tr("请输入 1–63 个短物体名称，按背景到前景排序，例如 wood table, saucer, cup, spoon。"));return false;
    }
    // We do not run executable instructions from a .draw file. This command
    // is explicitly invoked by the user with a local checkpoint and script.
    const QString checkpoint=sam3Checkpoint_.isLocalFile()?sam3Checkpoint_.toLocalFile():qEnvironmentVariable("DRAWBORAD_SAM3_CHECKPOINT");
    const QString python=qEnvironmentVariable("DRAWBORAD_SAM3_PYTHON",QStringLiteral("python"));
    const QString script=qEnvironmentVariable("DRAWBORAD_SAM3_SCRIPT",
        QCoreApplication::applicationDirPath()+QStringLiteral("/sam3_semantic.py"));
    const QString device=qEnvironmentVariable("DRAWBORAD_SAM3_DEVICE",QStringLiteral("cuda"));
    if(checkpoint.isEmpty() || !QFileInfo(checkpoint).isFile() || !QFileInfo(script).isFile() ||
       (device!="cpu" && device!="cuda")){
        fail(tr("需要本地 SAM 3 图像模型检查点与脚本。请设置 DRAWBORAD_SAM3_CHECKPOINT；脚本默认在应用目录。"));return false;
    }
    auto directory=std::make_unique<QTemporaryDir>();
    if(!directory->isValid()){fail(tr("无法创建本地语义分层任务目录。"));return false;}
    const auto current=bridge_.canvasSnapshot();
    semanticBaseDocument_=current?std::optional<DocumentId>{current->documentId}:std::nullopt;
    semanticBaseRevision_=current?current->revision:0;
    semanticDirectory_=std::move(directory);
    semanticCancelled_=false;
    semanticBusy_=true;
    auto* process=new QProcess(this);
    semanticProcess_=process;
    QStringList args{script,"--input",image.absoluteFilePath(),"--output",
        semanticDirectory_->path(),"--checkpoint",checkpoint,"--device",device};
    for(const auto& prompt:prompts)args<<"--prompt"<<prompt;
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError reason){
        if(reason!=QProcess::FailedToStart || semanticProcess_!=process)return;
        semanticBusy_=false;semanticProcess_=nullptr;semanticDirectory_.reset();
        process->deleteLater();fail(tr("SAM 3 解释器无法启动：%1").arg(process->errorString()));
    });
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,[this,process](int exitCode,QProcess::ExitStatus status){
        if(semanticProcess_!=process)return;
        const QString diagnostics=QString::fromUtf8(process->readAllStandardError()).right(1800);
        const QString manifest=semanticDirectory_?QDir(semanticDirectory_->path()).filePath("manifest.json"):QString();
        semanticBusy_=false;semanticProcess_=nullptr;
        process->deleteLater();
        if(semanticCancelled_){semanticDirectory_.reset();message_=tr("已取消 SAM 3 识别；原工程未改变。");emit changed();return;}
        if(status!=QProcess::NormalExit || exitCode!=0){
            semanticDirectory_.reset();fail(tr("SAM 3 推理失败（%1）：%2").arg(exitCode).arg(diagnostics));return;
        }
        const auto current=bridge_.canvasSnapshot();
        const auto id=current?std::optional<DocumentId>{current->documentId}:std::nullopt;
        if(id!=semanticBaseDocument_ || (current && current->revision!=semanticBaseRevision_)){
            semanticDirectory_.reset();fail(tr("AI 推理期间工程已变化；未覆盖新的笔迹。"));return;
        }
        const bool ok=importSemanticManifest(QUrl::fromLocalFile(manifest));
        semanticDirectory_.reset();
        if(!ok)return;
    });
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start(python,args);
    message_=tr("正在以本地 SAM 3 图像模型按语义识别对象。结束后可在原生审阅中检查各图层。");
    emit changed();return true;
}

bool WorkflowController::startImage(const QUrl& url){return startAnalysis(url,false);}
bool WorkflowController::startAnalysis(const QUrl& url,bool prompted){
    if(busy() || bridge_.running_)return false;
    if(!url.isLocalFile() || !modelPackage_.isLocalFile() || !modelReady()){
        fail(tr("请先选择本地 SAM 模型包并通过 CPU 推理检查，再导入本地图片。"));return false;
    }
    NewDocumentRequest request;request.dimensions={1,1,Unit::Px,300};
    const auto spec=validateDocument(request,{});if(!spec){fail(QString::fromStdString(spec.error().messageKey));return false;}
    auto document=Document::create(spec.value(),uuid());if(!document){fail(QString::fromStdString(document.error().messageKey));return false;}
    auto directory=std::make_unique<QTemporaryDir>();
    if(!directory->isValid()){fail(tr("无法创建任务临时目录；当前工程未改变。"));return false;}
    QJsonObject options{{"type",ReviewRequestType},{"image",url.toLocalFile()}, {"package",modelPackage_.toLocalFile()},{"outputDirectory",directory->path()},{"modelReadiness",*modelReceipt_}};
    if(prompted){
        QJsonArray points;for(const auto& p:promptPoints_)points.append(QJsonObject{{"x",p.x},{"y",p.y},{"positive",p.positive}});
        options.insert("proposalMode","prompted");options.insert("points",points);
        if(promptBox_)options.insert("box",QJsonArray{promptBox_->x0,promptBox_->y0,promptBox_->x1,promptBox_->y1});
        const auto source=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(sourceMetadata_->bytes->data()),qsizetype(sourceMetadata_->bytes->size()))).object();options.insert("expectedSourceHash",source.value("sourceHash"));
    }
    auto metadata=jsonResource(options);
    const auto installed=document.value().apply(Command{InstallResource{metadata}},document.value().snapshot().revision);
    if(!installed){fail(QString::fromStdString(installed.error().messageKey));return false;}
    const auto snapshot=document.value().snapshot();
    auto bytes=encodeDelta({},snapshot);auto hash=stateHash(snapshot);if(!bytes || !hash){fail(QString::fromStdString(!bytes?bytes.error().messageKey:hash.error().messageKey));return false;}
    JobRequest job;job.id=uuid();job.nonce=uuid();job.task="image.review.sam21";
    job.inputSnapshot={snapshot.documentId,snapshot.revision,hash.value()};job.snapshotBytes=std::move(bytes.value());
    job.budget.ramSoftBytes=2*GiB;job.budget.ramReclaimBytes=3*GiB;
    job.resources={job.id,ResourceDevice::CPU,2*GiB,0,0,false};
    auto submitted=broker_.submit(job);if(!submitted){fail(QString::fromStdString(submitted.error().messageKey));return false;}
    const auto current=bridge_.canvasSnapshot();jobBaseDocument_=current?std::optional<DocumentId>{current->documentId}:std::nullopt;
    jobBaseRevision_=current?current->revision:0;
    appendPrompt_=prompted;promptBaseRevision_=review_?review_->revision():0;
    analysis_=std::move(document.value());jobDirectory_=std::move(directory);running_=std::move(job);progressCompleted_=0;progressTotal_=0;
    message_=tr("正在校验并加载本地模型；原工程保持不变，可取消。" );emit changed();return true;
}
void WorkflowController::cancel(){
    if(semanticProcess_ && semanticBusy_){semanticCancelled_=true;semanticProcess_->kill();message_=tr("正在取消 SAM 3 推理；原工程保留。");emit changed();return;}
    if(running_){broker_.cancel(running_->id);message_=tr("正在取消；保留当前工程和已有审阅。" );emit changed();}
}
void WorkflowController::poll(){
    if(!running_)return;
    broker_.poll();const auto status=broker_.status(running_->id);
    if(status==JobStatus::Cancelled || status==JobStatus::Failed){
        stopModelCheck(tr("检查已停止；请重新检查后再导入。"));running_.reset();analysis_.reset();jobDirectory_.reset();fail(tr("任务已停止；当前工程和已有审阅未改变。"));return;
    }
    const auto next=host_.pollMessage(running_->id);
    if(!next){broker_.workerFailed(running_->id);stopModelCheck(tr("后台模型检查失败；请重新检查。"));running_.reset();analysis_.reset();jobDirectory_.reset();fail(tr("后台任务失败；当前工程未改变。"));return;}
    if(!next.value())return;
    const auto& message=*next.value();
    if(message.kind==MessageKind::Progress && message.payload.size()==8){
        unsigned done=0,total=0;for(unsigned i=0;i<4;++i){done|=unsigned(message.payload[i])<<(i*8);total|=unsigned(message.payload[4+i])<<(i*8);}
        if(total==0 || done>total || total>unsigned(std::numeric_limits<int>::max()))return;
        progressCompleted_=int(done);progressTotal_=int(total);
        if(running_->task=="model.preflight.v1"){emit changed();return;}
        if(appendPrompt_ && running_->task=="image.review.sam21"){
            message_=done<total?tr("正在按点/框提示分割，可取消；已有候选和笔迹未变。" ):tr("提示推理完成，正在校验追加候选。" );emit changed();return;
        }
        if(running_->task!="image.review.sam21"){
            message_=tr("已规划对象 %1 / %2，正在实际渲染并测量；尚未提交，可取消。" ).arg(done).arg(total);emit changed();return;
        }
        message_=done<total?tr("自动采样 %1 / %2；结果尚未写入工程。" ).arg(done).arg(total):tr("采样完成，正在筛选与整理真实蒙版……");emit changed();
    }else if(message.kind==MessageKind::Cancelled){
        broker_.acknowledgeCancel(message.id,message.nonce);stopModelCheck(tr("已取消模型检查；请重新检查后再导入。"));running_.reset();analysis_.reset();jobDirectory_.reset();fail(tr("已取消；当前工程和已有审阅未改变。"));
    }else if(message.kind==MessageKind::Failure){
        const auto reason=QString::fromUtf8(reinterpret_cast<const char*>(message.payload.data()),qsizetype(message.payload.size()));
        broker_.workerFailed(running_->id);stopModelCheck(tr("模型检查失败：%1").arg(reason));
        if(running_->task=="image.review.sam21" && (reason.startsWith("model.") || reason=="review.model-preflight-required-or-stale")){modelReceipt_.reset();modelState_=QStringLiteral("NotChecked");modelStatus_=tr("模型包已变化或推理不可用：%1。请重新检查。").arg(reason);}
        running_.reset();analysis_.reset();jobDirectory_.reset();fail(tr("分析失败：%1。当前工程未改变。" ).arg(reason));
    }else if(message.kind==MessageKind::Result){
        const auto jobId=running_->id;const auto task=running_->task;
        QFile file(QDir(jobDirectory_->path()).filePath("review.delta"));
        bool ok=message.payload.size()==32 && file.size()>0 && file.size()<=qint64(MaxSnapshotMappingBytes) && file.open(QIODevice::ReadOnly);
        Bytes bytes;
        if(ok){const auto data=file.readAll();bytes.assign(reinterpret_cast<const std::uint8_t*>(data.constData()),reinterpret_cast<const std::uint8_t*>(data.constData())+data.size());ok=std::equal(message.payload.begin(),message.payload.end(),sha256(bytes).bytes.begin());}
        file.close(); // Close before retiring the directory on Windows.
        if(ok){
            JobResult result;result.id=message.id;result.nonce=message.nonce;result.inputRevision=running_->inputSnapshot.revision;
            result.outputHash=sha256(bytes);result.delta=std::move(bytes);
            ok=bool(broker_.accept(result,*analysis_));
            if(ok)ok=task=="model.preflight.v1"?loadModelResult(analysis_->snapshot()):task=="image.review.sam21"?(appendPrompt_?loadPromptResult(analysis_->snapshot()):loadResult(analysis_->snapshot())):loadPaintResult(analysis_->snapshot());
        }
        if(!ok){broker_.workerFailed(jobId);stopModelCheck(tr("模型检查结果校验失败，请重新检查。"));}
        running_.reset();analysis_.reset();jobDirectory_.reset();
        if(!ok)fail(tr("候选校验失败；当前工程和已有审阅未改变。"));else emit changed();
    }
}
bool WorkflowController::loadResult(const DocumentSnapshot& snapshot){
    const auto current=bridge_.canvasSnapshot();
    if(jobBaseDocument_!=(current?std::optional<DocumentId>{current->documentId}:std::nullopt) ||
       (current && jobBaseRevision_!=current->revision))return false;
    const auto result=findJsonResource(snapshot,ReviewResultType),source=findJsonResource(snapshot,SourceReferenceType);
    if(!result || !source)return false;
    const auto width=result->value("width").toInteger(),height=result->value("height").toInteger();
    if(width<=0 || height<=0 || width*height>4*1024*1024)return false;
    MaskSet masks;masks.dimensions={std::uint64_t(width),std::uint64_t(height)};
    ProposalSet proposals;proposals.sourceSize=masks.dimensions;
    std::vector<double> scores;std::vector<QString> names;
    const auto array=result->value("candidates").toArray();if(array.isEmpty() || array.size()>256)return false;
    for(const auto& item:array){
        const auto object=item.toObject();const auto hash=parseHash(object.value("mask").toString());
        if(!hash || !snapshot.resources->contains(*hash))return false;
        const auto& resource=snapshot.resources->at(*hash);
        const auto score=object.value("score").toDouble(std::numeric_limits<double>::quiet_NaN());
        if(resource.kind!=ResourceKind::Mask || resource.bytes->size()!=std::size_t(width*height) || !std::isfinite(score))return false;
        masks.fullResolutionMasks.push_back(*resource.bytes);
        proposals.candidates.push_back({Id::fromNumber(names.size()+1),{}, {},score});
        scores.push_back(score);names.push_back(tr("候选 %1").arg(names.size()+1));
    }
    auto review=ReviewSession::create(proposals,masks,0);if(!review)return false;
    // Keep the old review and its undo history until every part of the new
    // review, including a hash-matching preview, is ready to install.
    const auto pinned=pinSource(*source,directory_->path());if(pinned.isEmpty())return false;
    auto sourceMetadata=jsonResource(*source);
    sourceMetadata_=std::move(sourceMetadata);sourceUrl_=pinned;
    review_=std::move(review.value());names_=std::move(names);scores_=std::move(scores);selected_.assign(names_.size(),false);
    objects_.assign(names_.size(),{});baseMasks_.assign(names_.size(),{});background_.assign(names_.size(),false);undoUi_.clear();activeIndex_=0;pending_=true;
    promptPoints_.clear();promptBox_.reset();reviewAudit_=QJsonArray{QJsonObject{{"operation","automatic-proposals"},{"sourceHash",source->value("sourceHash")},{"modelHash",source->value("modelHash")},{"candidates",array.size()}}};
    baseDocument_=jobBaseDocument_;baseRevision_=jobBaseRevision_;
    message_=tr("得到 %1 个候选部件。请检查、命名并勾选需要的对象；尚未重绘。" ).arg(names_.size());updateOverlay();return true;
}
bool WorkflowController::loadPromptResult(const DocumentSnapshot& snapshot){
    if(!review_ || !sourceMetadata_ || review_->revision()!=promptBaseRevision_ || !canRecordEdit())return false;
    const auto result=findJsonResource(snapshot,ReviewResultType),source=findJsonResource(snapshot,SourceReferenceType);if(!result || !source || result->value("proposalMode").toString()!="prompted")return false;
    const auto original=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(sourceMetadata_->bytes->data()),qsizetype(sourceMetadata_->bytes->size()))).object();
    if(source->value("sourceHash")!=original.value("sourceHash") || result->value("width").toInteger()!=imageWidth() || result->value("height").toInteger()!=imageHeight())return false;
    const auto array=result->value("candidates").toArray();if(array.isEmpty() || array.size()>256)return false;
    MaskSet masks;masks.dimensions=review_->masks().dimensions;ProposalSet proposals;proposals.sourceSize=masks.dimensions;std::vector<double> scores;
    for(const auto& value:array){
        const auto candidate=value.toObject();const auto hash=parseHash(candidate.value("mask").toString());
        if(!hash || !snapshot.resources->contains(*hash))return false;const auto& resource=snapshot.resources->at(*hash);
        const auto score=candidate.value("score").toDouble(std::numeric_limits<double>::quiet_NaN());
        if(resource.kind!=ResourceKind::Mask || resource.bytes->size()!=masks.dimensions.width*masks.dimensions.height || !std::isfinite(score))return false;
        masks.fullResolutionMasks.push_back(*resource.bytes);proposals.candidates.push_back({Id::fromNumber(proposals.candidates.size()+1),{},{},score});scores.push_back(score);
    }
    const auto before=uiState();const auto oldSize=names_.size();const auto done=review_->appendProposals(proposals,masks,review_->revision());if(!done){fail(QString::fromStdString(done.error().messageKey));return false;}
    undoUi_.push_back(before);
    for(std::size_t i=0;i<scores.size();++i){names_.push_back(tr("提示候选 %1").arg(oldSize+i+1));scores_.push_back(scores[i]);selected_.push_back(false);objects_.push_back({});baseMasks_.push_back({});background_.push_back(false);}
    reviewAudit_.append(QJsonObject{{"operation","prompted-proposals"},{"sourceHash",source->value("sourceHash")},{"modelHash",source->value("modelHash")},{"modelReadiness",source->value("modelReadiness")},{"points",result->value("points")},{"box",result->value("box")},{"candidates",array}});
    activeIndex_=int(oldSize);pending_=true;message_=tr("已追加 %1 个真实提示候选，原有候选、蒙版编辑和笔迹未替换。请检查后勾选。" ).arg(scores.size());updateOverlay();return true;
}
void WorkflowController::updateOverlay(){
    overlayUrl_={};
    if(!review_ || (activeIndex_>=0 && std::size_t(activeIndex_)>=review_->masks().fullResolutionMasks.size()) || !directory_->isValid())return;
    const auto& masks=review_->masks();Bytes unassigned;
    if(activeIndex_<0){
        unassigned.assign(std::size_t(masks.dimensions.width*masks.dimensions.height),255);
        for(std::size_t i=0;i<selected_.size();++i)if(selected_[i])
            for(std::size_t p=0;p<unassigned.size();++p)if(masks.fullResolutionMasks[i][p])unassigned[p]=0;
    }
    const auto& bytes=activeIndex_<0?unassigned:masks.fullResolutionMasks[std::size_t(activeIndex_)];
    QImage image(int(masks.dimensions.width),int(masks.dimensions.height),QImage::Format_ARGB32);
    for(int y=0;y<image.height();++y){auto row=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)row[x]=qRgba(activeIndex_<0?240:40,activeIndex_<0?90:220,activeIndex_<0?80:200,bytes[std::size_t(y*image.width()+x)]*110/255);}
    const auto path=QDir(directory_->path()).filePath(QString("overlay-%1.png").arg(++overlayVersion_));
    if(image.save(path))overlayUrl_=QUrl::fromLocalFile(path);
}
bool WorkflowController::selectCandidate(int index){if(!review_ || index<-1 || (index>=0 && std::size_t(index)>=names_.size()) || busy())return false;activeIndex_=index;updateOverlay();emit changed();return true;}
bool WorkflowController::includeCandidate(int index,bool included){if(busy() || index<0 || std::size_t(index)>=names_.size())return false;selected_[std::size_t(index)]=included;if(activeIndex_<0)updateOverlay();emit changed();return true;}
bool WorkflowController::renameCandidate(int index,const QString& name){
    const auto clean=name.trimmed();if(busy() || index<0 || std::size_t(index)>=names_.size() || clean.isEmpty() || clean.size()>128)return false;
    if(names_[std::size_t(index)]!=clean){names_[std::size_t(index)]=clean;pending_=true;emit changed();}return true;
}
bool WorkflowController::editRectangle(bool add,double x0,double y0,double x1,double y1){
    if(busy() || !review_ || activeIndex_<0)return false;
    if(!canRecordEdit())return false;
    for(const auto n:{x0,y0,x1,y1})if(!std::isfinite(n))return false;
    const auto size=review_->masks().dimensions;
    const auto left=std::clamp(std::floor(std::min(x0,x1)),0.0,double(size.width));
    const auto top=std::clamp(std::floor(std::min(y0,y1)),0.0,double(size.height));
    const auto right=std::clamp(std::ceil(std::max(x0,x1)),0.0,double(size.width));
    const auto bottom=std::clamp(std::ceil(std::max(y0,y1)),0.0,double(size.height));
    if(right<=left || bottom<=top)return false;
    MaskEdit edit;edit.operation=add?MaskEditOp::AddBrush:MaskEditOp::SubtractBrush;edit.objectIndex=std::size_t(activeIndex_);
    edit.region={std::int64_t(left),std::int64_t(top),std::uint64_t(right-left),std::uint64_t(bottom-top)};edit.expectedRevision=review_->revision();
    if(!review_->apply(edit))return false;
    undoUi_.push_back(uiState());reviewAudit_.append(QJsonObject{{"operation",add?"add-rectangle":"subtract-rectangle"},{"index",activeIndex_},{"x",qint64(left)},{"y",qint64(top)},{"width",qint64(right-left)},{"height",qint64(bottom-top)}});pending_=true;updateOverlay();emit changed();return true;
}
bool WorkflowController::mergeIncluded(){
    if(busy() || !review_ || editingExisting() || activeIndex_<0 || !selected_[std::size_t(activeIndex_)] || std::count(selected_.begin(),selected_.end(),true)!=2)return false;
    if(!canRecordEdit())return false;
    std::size_t other=0;while(!selected_[other] || other==std::size_t(activeIndex_))++other;
    MaskEdit edit;edit.operation=MaskEditOp::Merge;edit.objectIndex=std::size_t(activeIndex_);edit.secondaryIndex=other;edit.expectedRevision=review_->revision();
    if(!review_->apply(edit))return false;
    undoUi_.push_back(uiState());
    names_.erase(names_.begin()+std::ptrdiff_t(other));scores_.erase(scores_.begin()+std::ptrdiff_t(other));selected_.erase(selected_.begin()+std::ptrdiff_t(other));
    objects_.erase(objects_.begin()+std::ptrdiff_t(other));baseMasks_.erase(baseMasks_.begin()+std::ptrdiff_t(other));background_.erase(background_.begin()+std::ptrdiff_t(other));
    reviewAudit_.append(QJsonObject{{"operation","merge-visible-parts"},{"target",activeIndex_},{"other",qint64(other)}});
    if(other<std::size_t(activeIndex_))--activeIndex_;pending_=true;updateOverlay();emit changed();return true;
}
bool WorkflowController::undo(){
    if(busy() || !review_ || undoUi_.empty() || !review_->undo())return false;
    auto state=std::move(undoUi_.back());undoUi_.pop_back();names_=std::move(state.names);scores_=std::move(state.scores);selected_=std::move(state.selected);activeIndex_=state.active;
    objects_=std::move(state.objects);baseMasks_=std::move(state.masks);background_=std::move(state.background);
    // Undo records retain prefix lengths, never shared copies of every audit
    // prefix. Keeping the latter makes each append detach and retain O(N²) data.
    while(reviewAudit_.size()>state.auditSize)reviewAudit_.removeLast();
    pending_=true;updateOverlay();emit changed();return true;
}
bool WorkflowController::canRecordEdit(){
    const auto pixels=review_->masks().dimensions.width*review_->masks().dimensions.height;
    auto bytes=pixels*(names_.size()+1); // Proposed snapshot, including unassigned.
    for(const auto& entry:undoUi_)bytes+=pixels*(entry.names.size()+1);
    if(bytes>128*1024*1024){fail(tr("蒙版撤销记录已达到内存限额。请先应用当前修改，或撤销后继续。"));return false;}
    return true;
}
bool WorkflowController::accept(){
    if(busy() || !review_ || !sourceMetadata_)return false;
    const auto reviewed=review_->accept(review_->revision());
    if(!reviewed){fail(QString::fromStdString(reviewed.error().messageKey));return false;}
    const auto current=bridge_.canvasSnapshot();
    if(baseDocument_!=(current?std::optional<DocumentId>{current->documentId}:std::nullopt) ||
       (!editingExisting() && current && baseRevision_!=current->revision)){
        fail(tr("当前绘画已变化。请先保存，并重新导入或重开审阅，避免覆盖新笔迹。"));return false;
    }
    if(editingExisting())for(std::size_t i=0;i<objects_.size();++i){
        if(objects_[i].empty())continue;
        if(!current || !current->nodes->contains(objects_[i]) || current->nodes->at(objects_[i]).maskId!=baseMasks_[i]){
            fail(tr("对象蒙版已被其他操作改变，请重开审阅。笔迹保持不变。"));return false;
        }
    }
    Command command;command.operations.push_back(InstallResource{*sourceMetadata_});
    command.operations.emplace_back(InstallResource{jsonResource({{"type","drawborad.review-audit.v1"},{"operations",reviewAudit_},{"humanQualityApproval",false}})});
    ApplicationController next;
    if(!editingExisting()){
        NewDocumentRequest request;request.dimensions={double(imageWidth()),double(imageHeight()),Unit::Px,300};
        if(!next.newDocument(request))return false;
    }
    std::size_t included=0;
    for(std::size_t i=0;i<names_.size();++i){
        if(!selected_[i])continue;
        const auto& bytes=reviewed.value().masks[i];
        if(std::none_of(bytes.begin(),bytes.end(),[](auto value){return value>0;})){fail(tr("勾选的蒙版为空，请修改或取消勾选。"));return false;}
        Resource mask{sha256(bytes),ResourceKind::Mask,std::make_shared<const Bytes>(bytes),{sourceMetadata_->id}};
        command.operations.push_back(InstallResource{mask});
        if(!objects_[i].empty()){
            if(current->nodes->at(objects_[i]).maskId!=mask.id)command.operations.push_back(SetMask{objects_[i],mask.id});
            if(current->nodes->at(objects_[i]).name!=names_[i].toUtf8().toStdString())command.operations.push_back(RenameNode{objects_[i],names_[i].toUtf8().toStdString()});
        }else{
            LayerNode object;object.id=uuid();object.parent=Document::rootId();object.kind=NodeKind::Object;
            object.name=names_[i].toUtf8().toStdString();object.maskId=mask.id;object.bounds={0,0,std::uint64_t(imageWidth()),std::uint64_t(imageHeight())};
            LayerNode layer;layer.id=uuid();layer.parent=object.id;layer.objectId=object.id;layer.stage="paint";
            layer.name=object.name+" / Paint";layer.bounds=object.bounds;
            command.operations.push_back(AddNode{object,background_[i]?0:SIZE_MAX});command.operations.push_back(AddNode{layer});
        }
        ++included;
    }
    if(!included){fail(tr("请明确勾选至少一个需要接受的对象。"));return false;}
    const auto applied=editingExisting()?bridge_.app_.apply(command):next.apply(command);
    if(!applied){fail(QString::fromStdString(applied.error().messageKey));return false;}
    if(!editingExisting()){bridge_.app_=std::move(next);bridge_.replayCount_=-1;++bridge_.canvasGeneration_;bridge_.referenceUrl_={};bridge_.referenceFeedback_.clear();bridge_.resetLayerSelection();}
    const auto accepted=*bridge_.app_.snapshot();restore(accepted);
    message_=tr("已应用勾选对象和蒙版；已有笔迹保留，新增绘制层为空。未勾选的已有对象保持原样。");emit changed();return true;
}
void WorkflowController::reset(){
    if(semanticProcess_){semanticCancelled_=true;semanticProcess_->disconnect(this);semanticProcess_->kill();semanticProcess_->deleteLater();semanticProcess_=nullptr;}
    semanticBusy_=false;semanticDirectory_.reset();semanticBaseDocument_.reset();semanticBaseRevision_=0;
    stopModelCheck(tr("模型检查已停止；请重新检查后再导入。"));
    if(running_)broker_.workerFailed(running_->id);
    running_.reset();analysis_.reset();jobDirectory_.reset();review_.reset();names_.clear();scores_.clear();selected_.clear();objects_.clear();baseMasks_.clear();sourceMetadata_.reset();undoUi_.clear();
    background_.clear();promptPoints_.clear();promptBox_.reset();reviewAudit_={};appendPrompt_=false;promptBaseRevision_=0;
    sourceUrl_={};overlayUrl_={};baseDocument_.reset();baseRevision_=0;activeIndex_=-1;pending_=false;progressCompleted_=0;progressTotal_=0;message_.clear();emit changed();
}
bool WorkflowController::startPaintJob(QJsonObject options){
    const auto current=bridge_.canvasSnapshot();
    if(busy() || bridge_.running_ || pending_ || !editingExisting() || !current || !sourceMetadata_ || sourceUrl_.isEmpty()){
        fail(tr("请先应用对象审阅，并提供哈希一致的本地原图；绘画和回放不依赖原图。"));return false;
    }
    // Saved project metadata is data, never authority to execute a worker task
    // or choose its output directory. Preserve it but refuse reserved directives.
    if(findJsonResource(*current,ReviewRequestType) || findJsonResource(*current,ReconstructionRequestType) || findJsonResource(*current,ModelPreflightRequestType)){
        fail(tr("工程包含保留的后台任务指令，已拒绝执行；当前绘画和文件保持不变。"));return false;
    }
    auto directory=std::make_unique<QTemporaryDir>();if(!directory->isValid())return false;
    options.insert("type",ReconstructionRequestType);options.insert("image",sourceUrl_.toLocalFile());options.insert("outputDirectory",directory->path());
    auto snapshot=*current;auto resources=std::make_shared<ResourceMap>(*snapshot.resources);auto instruction=jsonResource(options);
    resources->emplace(instruction.id,instruction);snapshot.resources=resources;
    auto document=Document::fromSnapshot(snapshot);if(!document){fail(QString::fromStdString(document.error().messageKey));return false;}
    auto bytes=encodeDelta({},snapshot);auto hash=stateHash(snapshot);if(!bytes || !hash)return false;
    JobRequest job;job.id=uuid();job.nonce=uuid();job.task=options.value("mode").toString()=="refine"?"image.refine.v1":"image.reconstruct.v1";
    job.inputSnapshot={snapshot.documentId,snapshot.revision,hash.value()};job.snapshotBytes=std::move(bytes.value());
    job.budget.ramSoftBytes=GiB;job.budget.ramReclaimBytes=2*GiB;job.resources={job.id,ResourceDevice::CPU,GiB,0,0,false};
    auto submitted=broker_.submit(job);if(!submitted){fail(QString::fromStdString(submitted.error().messageKey));return false;}
    paintBase_=*current;analysis_=std::move(document.value());jobDirectory_=std::move(directory);running_=std::move(job);progressCompleted_=0;progressTotal_=0;
    message_=tr("正在从原图规划真实笔迹，结果将分阶段写入对象层；可取消，当前笔迹保持不变。" );emit changed();return true;
}
bool WorkflowController::loadPaintResult(const DocumentSnapshot& result){
    const auto current=bridge_.canvasSnapshot();
    if(!current || !paintBase_ || current->revision!=paintBase_->revision || !current->sameContent(*paintBase_))return false;
    auto command=reconstructionCommand(*current,result);if(!command)return false;
    auto committed=bridge_.app_.apply(command.value());if(!committed)return false;
    QJsonObject report;
    for(const auto& [id,resource]:*result.resources)if(!current->resources->contains(id) && resource.kind==ResourceKind::Reference){
        const auto& b=*resource.bytes;const auto json=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(b.data()),qsizetype(b.size())));
        if(json.isObject() && json.object().value("type").toString()==QLatin1String(ReconstructionResultType))report=json.object();
    }
    const auto accepted=*bridge_.canvasSnapshot();bridge_.replayCount_=-1;bridge_.resetLayerSelection();restore(accepted);
    message_=tr("已追加 %1 条真实笔迹。本次实测区域占整图 %2%；颜色 MAE %3，透明度 MAE %4。需要审阅：尚未通过整图保真与自然过程验收。" )
        .arg(report.value("events").toInteger()).arg(report.value("acceptedImageFraction").toDouble()*100,0,'f',2)
        .arg(report.value("acceptedColorMae").toDouble(),0,'f',4).arg(report.value("acceptedAlphaMae").toDouble(),0,'f',4);return true;
}
bool WorkflowController::startReconstruction(){return startPaintJob({{"mode","reconstruct"}});}
bool WorkflowController::relinkSource(const QUrl& url){
    const auto current=bridge_.canvasSnapshot();
    if(busy() || pending_ || !current || !sourceMetadata_ || !editingExisting() || !url.isLocalFile())return false;
    const auto& bytes=*sourceMetadata_->bytes;
    auto object=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size()))).object();object.insert("image",url.toLocalFile());
    const auto pinned=pinSource(object,directory_->path());
    if(pinned.isEmpty()){fail(tr("原图哈希不一致或文件不可读；未更改任何笔迹和蒙版。"));return false;}
    const auto source=jsonResource(object);Command command;command.operations.emplace_back(InstallResource{source});std::set<Hash> masks;
    for(const auto id:objects_)if(current->nodes->contains(id))masks.insert(current->nodes->at(id).maskId);
    for(const auto id:masks){
        auto deps=current->resources->at(id).dependencies;
        for(auto& dependency:deps)if(dependency==sourceMetadata_->id)dependency=source.id;
        command.operations.emplace_back(SetDependencies{id,std::move(deps)});
    }
    const auto done=bridge_.app_.apply(command);if(!done){fail(QString::fromStdString(done.error().messageKey));return false;}
    restore(*bridge_.canvasSnapshot());message_=tr("已按完整文件哈希重新关联原图。原有笔迹和蒙版像素保持不变。" );emit changed();return true;
}
bool WorkflowController::refineRectangle(double x0,double y0,double x1,double y1){
    if(activeIndex_<0 || std::size_t(activeIndex_)>=objects_.size())return false;
    for(const auto value:{x0,y0,x1,y1})if(!std::isfinite(value))return false;
    const auto x=std::clamp(std::floor(std::min(x0,x1)),0.0,double(imageWidth()));
    const auto y=std::clamp(std::floor(std::min(y0,y1)),0.0,double(imageHeight()));
    const auto right=std::clamp(std::ceil(std::max(x0,x1)),0.0,double(imageWidth()));
    const auto bottom=std::clamp(std::ceil(std::max(y0,y1)),0.0,double(imageHeight()));
    if(right<=x || bottom<=y)return false;
    const auto id=objects_[std::size_t(activeIndex_)];
    return startPaintJob({{"mode","refine"},{"object",QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.bytes.data()),16).toHex())},
        {"x",qint64(x)},{"y",qint64(y)},{"width",qint64(right-x)},{"height",qint64(bottom-y)}});
}
bool WorkflowController::exportPsd(const QUrl& url){
    const auto snapshot=bridge_.canvasSnapshot();
    if(busy() || pending_ || !snapshot || !url.isLocalFile())return false;
    auto painted=renderPaintedDocument(*snapshot,{snapshot->revision});if(!painted){fail(QString::fromStdString(painted.error().messageKey));return false;}
    PsdWriteRequest request;request.path=std::filesystem::path(url.toLocalFile().toStdWString());request.size=snapshot->spec.pixelSize;
    request.format=snapshot->spec.colorSpec.format;request.profile=snapshot->spec.colorSpec.profileId;request.ppi=snapshot->spec.ppi;
    if(!request.profile.empty()){
        const auto found=snapshot->resources->find(request.profile);
        if(found==snapshot->resources->end() || found->second.kind!=ResourceKind::Profile){fail(tr("缺少输出 ICC 资源，未导出。"));return false;}
        request.iccProfile=*found->second.bytes;
    }
    request.layers=std::move(painted.value().layers);request.merged=std::move(painted.value().merged);
    const auto written=writePsd(request);if(!written){fail(QString::fromStdString(written.error().messageKey));return false;}
    message_=tr("已从笔迹渲染分层 PSD（对象 / 阶段、蒙版、独立图层像素）。Photoshop 实机重开尚未验证。" );emit changed();return true;
}
void WorkflowController::restore(const DocumentSnapshot& snapshot){
    reset();MaskSet masks;masks.dimensions=snapshot.spec.pixelSize;ProposalSet proposals;proposals.sourceSize=masks.dimensions;
    if(masks.dimensions.width>4096 || masks.dimensions.height>4096 || masks.dimensions.width*masks.dimensions.height>4*1024*1024)return;
    const auto source=associatedSource(snapshot);if(!source)return;
    auto metadata=jsonResource(*source);
    for(const auto& [id,node]:*snapshot.nodes){
        if(node.kind!=NodeKind::Object || node.maskId.empty() || !snapshot.resources->contains(node.maskId))continue;
        const auto& resource=snapshot.resources->at(node.maskId);
        if(resource.kind!=ResourceKind::Mask || resource.bytes->size()!=masks.dimensions.width*masks.dimensions.height ||
           std::find(resource.dependencies.begin(),resource.dependencies.end(),metadata.id)==resource.dependencies.end())continue;
        if(names_.size()==256 || (names_.size()+2)*masks.dimensions.width*masks.dimensions.height>56*1024*1024){
            reset();fail(tr("此工程的蒙版超出审阅内存限额；绘画工程仍可打开，未加载蒙版审阅。"));return;
        }
        masks.fullResolutionMasks.push_back(*resource.bytes);proposals.candidates.push_back({id,{}, {},0});
        names_.push_back(QString::fromUtf8(node.name));scores_.push_back(std::numeric_limits<double>::quiet_NaN());selected_.push_back(true);objects_.push_back(id);baseMasks_.push_back(node.maskId);background_.push_back(false);
    }
    if(names_.empty())return;
    auto review=ReviewSession::create(proposals,masks,snapshot.revision);if(!review)return;
    review_=std::move(review.value());sourceMetadata_=std::move(metadata);baseDocument_=snapshot.documentId;baseRevision_=snapshot.revision;activeIndex_=0;
    sourceUrl_=pinSource(*source,directory_->path());
    message_=sourceUrl_.isEmpty()?tr("蒙版已恢复；原图缺失或哈希不符。绘画回放不依赖原图。" ):tr("已恢复对象蒙版；原图仅供独立审阅。" );
    updateOverlay();emit changed();
}
}
