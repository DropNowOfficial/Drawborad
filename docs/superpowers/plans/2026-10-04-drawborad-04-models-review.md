# 离线模型、自定义包、对象分层与复核 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 任意支持输入可获得可修正的对象候选，并能完全离线选择兼容自定义模型。
**Architecture:** 模型提供候选而非最终真理；受限包验证、推理、全分辨率mask与Review独立。队列只允许一个重GPU任务，提交经过revision检查。
**Tech Stack:** C++20 / ONNX Runtime CPU-CUDA / SAM2.1 image adapter / native mask tiles。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

继承[总计划](2026-10-04-drawborad-implementation-plan.md)和[契约](drawborad-contracts.md)。Win11/185H/32GB/4070Laptop8GB；512²块、RAM8–12/18GiB、GPU2/6GiB软预算、1重GPU任务。完全离线含标准/高质量核心包；自定义不是任意Python/DLL插件。RGB/CMYK8/16权威数据不被RGB分析覆盖。4K/64层常规、128压力、8K后台。所有产品测试 **NotRun**。

## Review Focus

F05/M06旧结果不能覆盖；C01/C02/E04文字颜色单验；F04/C07/G04复杂路径与持久化；M01/M05/G05模型shape/解压/代码安全；R02/R05/R06/U06设备/显示边界。未经实际推理验证，不用参数量推断8GB机器性能。

## File Structure

`src/models`本地包、ORT session、adapter；`src/segmentation`候选与原尺寸投影；`src/review`人工确认；`src/jobs`队列；`tools/models`只在开发机导出/资格核验，终端用户不运行pip/npm。每task注册`cmake/TestTargets.cmake`。

### Task M01: 本地模型包与可信边界
**Dependencies:** F05。
**Files:** Create `src/models/ModelManifest.h`, `src/models/ModelRegistry.cpp`, `src/models/PackageVerifier.cpp`, `schemas/model-package.schema.json`; Test `tests/models/PackageTests.cpp`。
**Interfaces:** ConsumesPackagePath/TrustDecision/ModelManifest/ResourceBudget；Produces `Result<ModelId> ModelRegistry::install(const PackagePath&, TrustDecision)`, `Result<void> ModelRegistry::remove(ModelId)`, `Result<ModelManifest> validatePackage(const PackagePath&)`。
- [ ] **Step 1: Tests.** 有效包、缺hash、错weights、目录穿越、symlink、外部URL、script/pickle/DLL/custom-op、重复安装、使用中卸载。
```cpp
CHECK(tampered_package.error == ErrorCode::DigestMismatch);
CHECK(external_data_escape.error == ErrorCode::PathEscape);
CHECK(unsigned_without_consent.installed == false);
CHECK(package_script.executed == false);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M01 -Phase Red`。
- [ ] **Step 3: Implement.** manifest schema版本/任务/张量/ABI/provider/预后处理/许可/预算；安全本地staging校验后安装。签名可信也检查形状路径；未签用户包明确同意不绕过校验。卸载只删除自有包且不可卸活跃session。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M01 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M01 offline model packages"`；不stage权重/用户包。
**Accept:** 标准/高质量/自定义均来自已校验本地文件；无网络补依赖。

### Task M02: SAM2.1 Tiny/Small资格与锁定运行库
**Dependencies:** M01, C03, F01。
**Files:** Create `tools/models/export_sam21.py`, `tools/models/qualify.py`, `models/recipes/sam21-tiny.json`, `models/recipes/sam21-small.json`, `src/models/OrtSession.cpp`; Test `tests/models/ProviderParityTests.cpp`。
**Interfaces:** ConsumesInferenceRequest/ModelManifest/AnalysisView；Produces `Result<InferenceOutput> OrtSession::run(const InferenceRequest&, std::stop_token)`；工具CLI `qualify.py --recipe <json> --fixtures <manifest> --report <json>`。
- [ ] **Step 1: Tests.** 开发参考脚本生成有精确上游commit/权重hash的20个固定图/点/框输出；Tiny/Small CPU/CUDA与官方静态image路线比较。
```cpp
CHECK(mask_iou_against_reference >= 0.995);
CHECK(cpu_cuda_output_shape_equal);
CHECK(isolated_session.network_attempts == 0);
CHECK(peak_gpu_bytes <= admitted_budget);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M02 -Phase Red`；模型/硬件/oracle缺失为Blocked。
- [ ] **Step 3: Implement.** 按契约encoder/decoder规范化tensor名字/shape；保存完整导出环境/预处理。运行库DLL从锁定私有目录加载，不靠PATH/CUDA Toolkit；worker实际测峰值/取消/CPUfallback。Python仅开发转换，不进入运行依赖。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M02 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`；目标4070单独资格报告。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M02 qualified image segmentation runtime"`；只提交recipe/资格manifest，不提交权重。
**Accept:** 转换、许可、DLL、CPU/CUDA、显存门均实证；模型不自动命名所有对象。

### Task M03: 对象建议与原分辨率蒙版
**Dependencies:** M02, C01, F03。
**Files:** Create `src/segmentation/ProposalEngine.h`, `src/segmentation/ProposalEngine.cpp`, `src/segmentation/MaskProjection.cpp`; Test `tests/segmentation/ProposalTests.cpp`。
**Interfaces:** ConsumesAnalysisRequest/PromptSet/AnalysisView；Produces `Result<ProposalSet> ProposalEngine::propose(const AnalysisRequest&, std::stop_token)`, `Result<MaskSet> projectMasks(const ProposalSet&, const ProjectionPolicy&)`。
- [ ] **Step 1: Tests.** 极宽图、透明镂空、发丝、重叠/未覆盖、CMYK分析副本；校验逆变换而非简单拉伸。
```cpp
CHECK(full_res_masks.dimensions == source_dimensions);
CHECK(cmyk_source.channels_hash == hash_before_analysis);
CHECK(occluded_content.fabricated_pixels == 0);
CHECK(unassigned_region.is_explicit == true);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M03 -Phase Red`。
- [ ] **Step 3: Implement.** ICC→RGB分析视图、坐标transform/alpha另存；去重候选且保留unknown区域，边缘回到原尺寸细化。候选分数明确来源，不杜撰置信百分比；自动对象名只是建议，不把SAM称为语义命名器。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M03 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M03 editable object proposals"`。
**Accept:** 原尺寸可修mask、未观察透明区域不补画，分层失败仍可手动继续。

### Task M04: Review与完整人工修层
**Dependencies:** M03, R03, R05, F03。
**Files:** Create `src/review/ReviewSession.h`, `src/review/ReviewSession.cpp`, `src/review/MaskOperations.cpp`; Test `tests/review/MaskEditTests.cpp`。
**Interfaces:** ConsumesProposalSet/MaskEdit/PromptSet/Revision；Produces `Result<void> ReviewSession::apply(const MaskEdit&)`, `Result<ReviewedObjects> ReviewSession::accept(Revision)`, `Result<ProposalSet> ReviewSession::refine(const PromptSet&, std::stop_token)`。
- [ ] **Step 1: Tests.** 点/框正负提示、加减画笔、羽化/扩缩/去杂边、变换/合并/拆分、锁mask、undo、revision竞争。
```cpp
CHECK(feather_then_undo.mask_hash == original_hash);
CHECK(edit_object_a.object_b_hash == before_b);
CHECK(accept_old_revision.error == ErrorCode::StaleRevision);
CHECK(unreviewed.can_reconstruct == false);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M04 -Phase Red`。
- [ ] **Step 3: Implement.** 原分辨率tile mask命令，prompt结果只更新session；一键接受明确事务；锁层和当前revision生效；蒙版工具半径以文档px定义，不随zoom改变。只接受可追踪的ReviewedObjects进入重绘。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M04 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M04 review and mask correction"`。
**Accept:** R14工具真实操作mask，不是按钮占位；Review不自动跳过。

### Task M05: 自定义adapter与隔离兼容探测
**Dependencies:** M02, M03。
**Files:** Create `src/models/AdapterRegistry.h`, `src/models/AdapterRegistry.cpp`, `src/models/Sam21Adapter.cpp`, `src/models/CompatibilityProbe.cpp`; Test `tests/models/CustomAdapterTests.cpp`。
**Interfaces:** ConsumesModelId/DevicePreference/TensorSpec；Produces `Result<AdapterInfo> AdapterRegistry::resolve(std::string_view)`, `Result<CompatibilityReport> probeModel(ModelId, DevicePreference, std::stop_token)`。
- [ ] **Step 1: Tests.** 合法自定义sam21.image.v1、未知task、错误dtype/shape/动态N超限、NaN输出、shape bomb、OOM、CPUfallback。
```cpp
CHECK(custom_compatible_model.loaded_model_hash == selected_hash);
CHECK(unknown_task.error == ErrorCode::UnsupportedAdapter);
CHECK(bad_output_shape.committed_masks == 0);
CHECK(cpu_fallback.is_explicit_to_user);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M05 -Phase Red`。
- [ ] **Step 3: Implement.** 只执行已实现adapter声明式预后处理；隔离小输入probe测资源与张量。兼容不只看文件后缀；CPU/CUDA选择可见；没有兼容core模型明确阻断，不联网替代或冒充成功。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M05 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M05 custom model compatibility"`。
**Accept:** 用户可真正换兼容权重；未知架构明确拒绝而非任意代码插件。

### Task M06: 持久化队列与单重任务
**Dependencies:** M04, M05, F05。
**Files:** Create `src/jobs/ImageQueue.h`, `src/jobs/ImageQueue.cpp`, `src/jobs/QueueStore.cpp`; Test `tests/jobs/QueueTests.cpp`。
**Interfaces:** ConsumesInputPath/QueuePolicy/JobBroker/ReviewedObjects；Produces `Result<QueueId> ImageQueue::enqueue(std::span<const InputPath>, const QueuePolicy&)`, `void ImageQueue::pause(QueueId)`, `Result<void> ImageQueue::resume(QueueId)`。
- [ ] **Step 1: Tests.** 20图片队列、waitingReview、用户允许autoAccept分支、取消/重启/失败继续/旧job重复消息。
```cpp
CHECK(max_concurrent_heavy_gpu_jobs == 1);
CHECK(default_queue_state == QueueState::WaitingForReview);
CHECK(cancel_current.next_item_not_cancelled);
CHECK(restart.duplicate_commits == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id M06 -Phase Red`。
- [ ] **Step 3: Implement.** Queued→Analyzing→WaitingForReview→Reconstructing→Done明确状态机；每项失败/取消结果分离，写staging后原子提交；编辑中改revision则拒绝旧结果。暂停不杀编辑器，恢复读取已确认任务版本。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id M06 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: M06 recoverable image queue"`。
**Accept:** 无隐式Review跳过、并发吃满显存或重复提交。
