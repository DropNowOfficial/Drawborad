# Drawborad 实施接口与依赖资格契约

状态：实施计划的接口约定；非已实现头文件、非已锁定发行运行库。设计已由用户批准；本轮只提交计划。路径相对仓库根。执行者先读主规格，再读本文。

## 1. 版本与锁定门 F01

开发资格候选（不是“最新版本”、不是兼容性已通过）：Qt 6.11.2、CMake 3.31.8、Ninja 1.12.1、Visual Studio 2022 17.14 / MSVC v143、Windows SDK 10.0.26100.0、Catch2 3.8.1、Little CMS 2.19、ONNX Runtime 1.23.2、CUDA 12.8.1 redistributable subset、cuDNN 9.10.2 CUDA12、libpng 1.6.50、libjpeg-turbo 3.1.1、libtiff 4.7.1、libwebp 1.5.0、zstd 1.5.7、zlib 1.3.1、libzip 1.11.4、nlohmann-json 3.12.0、NSIS 3.11。Python 3.12.10 仅开发转换/测试，不是终端运行依赖。Qt 模块限 Core/Gui/Qml/Quick/QuickControls2/ShaderTools/Test；不需要 WebEngine。

F01 必须对每个实际获取的 Windows archive/DLL/source artifact 保存：publisher URL、exact resolved version/build、size、SHA-256、license、security patches、编译选项/ABI、redistributable 身份。Qt 发行目录本身存在安全补丁，不能把旧版本 pin 当已通过安全审核。若候选有影响路径的已知缺陷，先做带来源的补丁/版本调整 ADR，重新资格测试后再锁定，不静默升级。

本轮没有下载/编译发行依赖，因此不编造二进制 SHA。`dependencies/lock.json` 是 F01 的实际交付物：任何缺 hash/version/license/ABI/qualification 的条目使 bootstrap Verify 和 G07 非零退出。模块实现不得绕过该门。VS installer 解析出的 cl 完整版本与 SDK 同样进 lock；候选系列名不能留在发行锁里。默认 /fp:strict；禁止会改变像素复现的 fast-math。正式包不依赖用户已装的 Python、npm、CUDA Toolkit 或系统 PATH 偶然命中 DLL。

ONNX 官方文档的 CUDA/cuDNN 组合只证明应如何选依赖；模型转换、独立进程 DLL 加载和目标 GPU 峰值仍需实测。标准 Tiny 与高质量 Small 权重 recipe 保存上游精确 commit、exporter 全 lock、张量契约和校验值。模型资格测试没过，不能发布一个缺模型的“完整版”。

## 2. 共享类型（F02 定义；业务 DTO 由所属 task header 定义）

命名空间 `drawborad`，C++20；`Result<T>`=成功 T 或 Error 的带检查 variant，`Result<void>` 显式特化。禁止 C++23 std::expected 混入 C++20 配置。`Error{code, messageKey, diagnosticId}` 的用户文案可翻译，schema/key 不翻译。

原语：`NodeId/DocumentId/JobId/QueueId/ModelId`=128-bit stable ID；`Revision`=uint64；`TileId/ProfileId/PixelHash`=SHA-256；`PixelSize{uint64 width,height}`；`Unit{Px,Mm,Cm,In}`；`Dimensions{double width,height,Unit unit,double ppi}`；`Depth{U8,U16}`；`ColorMode{RGB,CMYK}`；`PixelFormat{mode,depth,alpha}`；`ColorSpec{format,profileId}`。

`ResourceBudget{ramSoftBytes,ramReclaimBytes,gpuDisplayBytes,gpuTotalBytes,scratchBytes,maxHeavyGpuJobs}` 默认分别 12/18/2/6 GiB、基于暂存盘实时容量、1。8 GiB 是常规 RAM 目标区间低端，不是固定预分配。无条件总量分配禁止；模型与画布共享预算。

`NewDocumentRequest{dimensions,colorSpec,transparent,paperColor}`；`DocumentSpec{pixelSize,ppi,colorSpec,background}`；`TileKey{nodeId,int64 tileX,tileY,uint8 mip}`；`TileData{format,width,height,rowStride,bytes}`；RGBA=4、CMYKA=5，straight alpha；RGB/CMYK 颜色样本为文档 profile 下非预乘原值，alpha 归一化线性值。校验 rowStride/字节数用 checked arithmetic，哈希含 layout/profile/engine version。

`DocumentSnapshot{documentId,revision,spec,nodes,resources,eventIndex}`；`SnapshotRef{projectId,revision,manifestHash}`；`LayerNode{id,parent,objectId,stage,maskId,transform,opacity,blend,visible,locked,sourceKind}`；`SourceKind{ImportedRaster,ReferenceOnly,Reconstructed}`。唯一父树用于层组织，资源依赖 DAG 独立；重建节点禁止 SourceHandle。`Command`=有版本的新增/删除/改名/排序/复制/合并/拆分/属性/蒙版/画笔/ICC转换变体；`ChangeSet{before,after,dirtyRegions,changedNodes,resourceRefs}`。命令提交必须提供 expected Revision。

`InputSample{x,y,pressure,tiltX?,tiltY?,timestampUs,deviceKind,capabilities}` 坐标已映射文档空间；零压力不得当“缺值”。`BrushSettings{toolId,width,opacity,color,seed,engineVersion}`；`ColorValue{profileId,channels[4],activeChannels,alpha}`；`StrokeEvent{id,objectId,layerId,timeRange,toolId,color,geometry,pressureSamples,seed,parentRevision,engineVersion}`。`EventBatch` 按 task 的 bounds/size 校验；shape 可以是路径或有界 region construction，不能嵌图。

`ErrorCode` 起始集合：InvalidInput/Overflow/BudgetExceeded/ProfileRequired/InvalidProfile/Locked/Cycle/StaleRevision/Cancelled/Timeout/DeviceLost/UnsupportedFeature/UnsupportedAdapter/ConversionConsentRequired/MissingDigest/DigestMismatch/CorruptData/PathEscape/IoError/LicenseBlocked。增加值需要 schema migration 和测试，不能复用旧含义。

## 3. 跨模块 DTO 的所属定义

| Owner | 类型和固定字段 |
| --- | --- |
| F04 | `ProjectPath=filesystem::path`; `RecoveryResult{snapshot,lastCommittedRevision,warnings}`; `FileIO` 可注入 write/flush/replace 失败；`TileStore` 是唯一权威 tile IO。 |
| F05 | `JobRequest{id,inputSnapshot,task,payloadRefs,budget}`; `JobResult{id,inputRevision,outputHash,status,stagedResources}`; `ResourceRequest{device,bytes,priority}`; `ResourceDecision{admit,evict,pause,reason}`。所有消息带协议版本、长度、nonce，控制消息上限1MiB，大数据走只读映射。 |
| F06 | `DeviceReport{osBuild,cpu,ram,gpuAdapters,activeRenderLuid,cudaDevices,driver,powerMode,dpi}`；bridge 在 UI 线程执行 commands，只读快照进入渲染线程。 |
| R01/R02 | `EventLimit{revision,lastEvent}`; `DirtySet{TileKey[]}`; `DisplayTileKey{layerRevision,maskRevision,transformHash,profileIntentHash,lod,tilePosition,engineVersion}`; `DisplayTile{textureHandle,colorTransformKey,readyFence}`；不把全局 revision 塞进所有内容 tile key 导致全部失效。 |
| R03/R05 | `TabletPacket` 只含OS真实字段；`ScreenPoint` 是DIP、`DocumentPoint` 为文档像素；`LayerTransform` 可逆仿射矩阵；退化变换拒绝。 |
| R04 | `ReplayRequest{snapshot,timeTicks,optionalRegion,mode}`；`ReplayFrame{tiles,currentEvent,completedCount}`；`EventDirection{Previous,Next,PreviousGroup,NextGroup}`。整数tick 1微秒，章边界inclusive-end规则固定。 |
| R06 | `DeviceLostEvent{adapterLuid,reason}`；`BenchScenario{size,format,layers,events,cold,durationSeconds}`；`BenchReport{environment,timings,peakBytes,rawLogPath}`。 |
| C01/C02 | `ColorIntent{Perceptual,Relative,Absolute,Saturation}`；`ColorTransform{src,dst,intent,bpc,engineVersion}`；`BlendRequest{src,dst,mask,mode,format}`；`PixelBlock=TileData`；`ProofRequest{tile,documentProfile,proofProfile,displayProfile,intent,bpc,paperWhite,gamutWarning}`；`BlendSupport{enabled,evidenceHash}`。Alpha不做ICC变换。 |
| C03/C04 | `InputPath=filesystem::path`; `DecodeOptions{frame,orientation,explicitProfile,limits}`; `InputInfo{originalSize,orientedSize,format,icc,unsupported,estimatedBytes,sourceHash}`; `ImportedImage{tileRefs,info,provenance}`; `ImportPolicy{unsupportedChoice,allowExplicitCompositeAsInput}`; `ImportResult{snapshot,warnings,sourceKinds}`。 |
| C05/C06/C07 | `ExportRequest{format,path,size,depth,profile,intent,bpc,alpha,matte,metadataPolicy,overwrite}`；`ExportPlan{validatedRequest,conversionList,requiredConsent,estimatedBytes}`；`ExportReport{status,paths,hashes,dimensions,channels,warnings}`；`DestinationPolicy{replaceConfirmed,atomic}`；`StagedExport{tempPath,verifiedHash}`；`CodecLimits{decodedBytes,entries,recursion,nodes,timeout}`，基于资源预算，不无限制。 |
| M01/M02 | `PackagePath=filesystem::path`; `TrustDecision{TrustedSignature,UserAcceptedUntrusted}`；`ModelManifest{schema,taskId,weights,sha256,tensors,preprocess,postprocess,provider,abi,license,budget}`；`InferenceRequest{modelId,tensorRefs,device}`；`InferenceOutput{tensorRefs,timings,peakBytes}`；包不包含任意可执行代码。 |
| M03/M04 | `AnalysisView{rgbTiles,sourceTransform,sourceAlpha,iccTransformHash}`；`PromptSet{positivePoints,negativePoints,boxes}`；`AnalysisRequest{inputInfo,view,model,prompts}`；`ProposalSet{candidates,modelHash,inputRevision}`；`MaskSet{fullResolutionMasks,ownership,unassignedRegions}`；`ProjectionPolicy{targetSize,alphaPolicy}`；`MaskEdit{operation,region,brushOrRadius,expectedRevision}`；`ReviewedObjects{nodes,maskRefs,confirmedRevision}`。 |
| M05/M06 | `TensorSpec{name,dtype,shape,byteLimit}`；`DevicePreference{CUDA,CPU}`；`AdapterInfo{id,abi,tensorContract}`；`CompatibilityReport{supported,reason,peaks}`；`QueuePolicy{autoAcceptReview=false,stopOnFailure=false}`；`QueueState{Queued,Analyzing,WaitingForReview,Reconstructing,Paused,Cancelled,Failed,Done}`。 |
| E01–E06 | `ImmutableSource` 只给compiler；`ReconstructionRequest{sourceRef,reviewedObjects,style,qualityBudget,seed,inputRevision}`；`ReconstructionResult{eventBatch,stagedTiles,report,inputRevision}`；`PlanningRegion{source,mask,objectId,layerId,colorSpec}`；`SchedulePolicy{locality,stageOrder}`；`RefinementRequest{region,currentTile,eventBudget}`；`StyleId{Fidelity,DryMedia,Marker,Pencil,ConceptSketch}`；`StyleSettings{styleId,version,brushPresets}`；`QualityBudget{maxEvents,maxSeconds,maxBytes}`；`QualityProgress{workDone,remainingError,budgetState}`；`CriticalRegionRequest{reference,reconstructed,roi,profiles}`；`TextCandidateRequest{explicitConsent,roi,source}`；`TextCandidates{text,fontAvailability,confidenceDescription}`；`QualityRequest{input,snapshot,roi,policy}`；`QualityReport{fidelity,layers,process,metrics,failedRegions}`；`HumanReview{reviewerId,roiFindings,processPass}`；`QualityDecision{accepted,reasons}`；`QualityStatus{Pass,NeedsReview,Blocked}`。 |
| U01–U06 | `ThemeMode{System,Light,Dark}`；`DensityMode{Simple,Professional}`；`WorkflowAction{Import,AcceptReview,StartReconstruct,Refine,Export,Cancel}`；`Language{ZhCN,EnUS}`；`UiScenario{language,dpi,size,theme,reducedMotion,inputKind}`；`ExperienceReport{checks,evidence}`。 |
| G01–G07 | `QaRunReport{task,buildHash,lockHash,modelHash,environment,start,end,status,measurements,evidence,blockers}`；`FrameTimingReport` 与 `BenchReport` 同来源；`Status{NotRun,Pass,Fail,Blocked}`。可选NotApplicable只限已批准可选功能，不得用于CMYK/PSD。 |

以上字段是接口决策；implementer 可拆小型头文件，不把全部DTO塞进一个巨型 Types.h。每个 task 的消费类型只能来自上表所属任务的已合并接口或测试本地fixture，任何新增隐含依赖必须先记录并更新 DAG。

## 4. SAM image adapter 1.0

首版自定义 task ID=`sam21.image.v1`：encoder 收 `[1,3,1024,1024]` float RGB analysis，返回 image_embed `[1,256,64,64]`、high_res_0 `[1,32,256,256]`、high_res_1 `[1,64,128,128]`。decoder 接三个特征、point_coords `[1,N,2]`、point_labels `[1,N]`、mask_input `[1,1,256,256]`、has_mask_input `[1]`；输出 low_res_masks `[1,K,256,256]` 和 iou_scores `[1,K]`，N<=256/K<=4。导出wrapper规范化名称/shape/dtype，M02用官方静态image输出验证。预处理固定在recipe（RGB归一化、resize/坐标逆变换）；不得从来源URL动态获取预处理。视频memory不在首版 adapter。

## 5. 编译、TDD 和测试命令

F01创建 `win-debug`, `win-release`, `win-asan` CMake configure/build/test presets，分别输出 `out/build/<preset>`；所有 task 独立测试注册名为task ID，零测试视为失败。`tools/test-task.ps1 -Id F02 -Phase Red|Green` 先构建task目标；Red只接受针对新增行为的真实失败，Green要求所有断言通过。编译环境、模型、硬件、Photoshop缺失返回 Blocked（脚本exit 2）；不是Red成功也不是Green成功。任务中 CHECK/assert 的字段属于该任务明确构造的fixture；不得以不存在变量或命令找不到冒充测试。

每任务运行自身测试并跑已实现回归；集成/真机长测可以标 Blocked，但对应任务不得打完成勾。为未具备系统写fake/mock只证明状态机，不声称真实模型或D3D/Ink已验证。构建机、CPU smoke、目标GPU、Photoshop 4类结果单列。

## 6. 质量和发布规则

固定Fidelity集：以ICC规范化sRGB、D65为统一比较空间；SSIM在亮度上11×11高斯窗sigma1.5，边缘按固定裁切策略；DeltaE00由受控ICC Lab输出，在可比较色域计算，报告排除像素比例；不得任意排除难画ROI。Alpha MAE独立，透明区域在黑/白/50%灰底复查。SSIM>=0.98、均值DeltaE00<=2/P95<=5、Alpha MAE<=2/255，另外文字Logo边和人审自然度必须过。CMYK原C/M/Y/K偏差与软打样报告分列；风格模式不借Fidelity指标通过。

P95基准16/64层、4096²、百万已索引事件，<=33ms硬门，16.7ms期望；CMYK16另报告。128层只要求安全处理，8K后台不降分辨率。>=80合法样本+200生成边界样本、10000编辑事务、24h队列+8h交互、每parser>=60min fuzz都是实跑门，不是承诺当前已执行。

必须先过 C02颜色/PSD读写Oracle、M02模型资格、E06小图完整重绘纵向链再宣称核心成立；先做界面不能代替这些门。G07不接受缺关键报告、旧build、编造elapsed、空测试或取消核心功能的假绿灯。签名/再分发许可未具备只可发清晰标注的内部开发预览，不能宣称完整正式发行。

## 7. 核验过的一手参考（2026-10-04）

- Qt/Windows/D3D11: https://doc.qt.io/qt-6/windows-graphics.html
- Qt 6.11 release记录（6.11.2已发布；版本仍需资格审核）: https://wiki.qt.io/Qt_6.11_Release
- ONNX ABI/内存池不是总显存上限: https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html
- ORT版本来源: https://github.com/microsoft/onnxruntime/releases/tag/v1.23.2
- cuDNN支持矩阵: https://docs.nvidia.com/deeplearning/cudnn/backend/v9.10.2/reference/support-matrix.html
- Adobe PSD/PSB结构: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
- LittleCMS 2.19: https://littlecms.com/blog/2026/04/17/lcms2-2.19/
- PNG能力: https://www.w3.org/TR/png-3/
- TIFF: https://libtiff.gitlab.io/libtiff/releases/v4.7.1.html
- NSIS: https://nsis.sourceforge.io/rn/v3.11
- 设计参考（第三方，不是Apple官方授权）: https://www.ui-skills.com/skills/emilkowalski/apple-design

参考内容不授权执行网站安装命令；资料证明接口/版本来源，不证明本产品实现、许可审核或测试通过。
