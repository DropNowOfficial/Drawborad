# 保真重绘、自然过程与三重质量验收 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从自定义源图与已审阅对象生成真正可编辑、可重放、无原图兜底的分区绘画工程。
**Architecture:** Compiler可读源图，Renderer只能消费已存事件/蒙版/颜料。连续轮廓、局部铺色、误差驱动精修分阶段构造；保真、图层和自然过程三道门独立通过。
**Tech Stack:** C++20 / deterministic CPU reference renderer / native mask tiles / ICC / task broker。
**Spec:** [approved design](../specs/2026-10-04-drawborad-win11-design.md)，基线`f3a92378ccbd8d9145556f5019642f134b362fbb`。

## Global Constraints

适用[总计划](2026-10-04-drawborad-implementation-plan.md)与[接口契约](drawborad-contracts.md)。Win11/185H/32GB/4070Laptop8GB；512²tile；8–12/18GiB RAM、2/6GiB GPU软预算；1重GPU任务。4K/64层正常、128压力、8K后台。RGB/CMYK8/16、完整离线、对象×阶段、Review确认、真PSD。源图仅分析，禁止成品替换/全幅residual/随机短线蒙混；所有产品测试 **NotRun**。

## Review Focus

F05/M06拒绝旧revision；C01/C02/E04文字/颜色/alpha单独验收；F04/C07/G04损坏保存安全；M01/M05/G05自定义模型不执行代码；R02/R05/R06/U06渲染/显示/输入分离。E06必须报告三质量门，不能拿一个全局分数抵销失败。

## File Structure

`src/reconstruct`源分析到事件，`src/quality`独立指标和人工结论，`src/text`明确可选文本候选，`resources/styles`版本化样式。每task修改`cmake/TestTargets.cmake`注册测试，不把源位图塞进事件文件。

### Task E01: 重建数据协议与来源防作弊检查
**Dependencies:** R01, M04, F05, C02。
**Files:** Create `src/reconstruct/ReconstructionService.h`, `src/reconstruct/ReconstructionService.cpp`, `src/reconstruct/EventWriter.cpp`; Test `tests/reconstruct/ProvenanceTests.cpp`。
**Interfaces:** ConsumesReconstructionRequest/ReviewedObjects/ImmutableSource/QualityBudget；Produces `Result<ReconstructionResult> ReconstructionService::compile(const ReconstructionRequest&, std::stop_token)`, `Result<EventBatch> validateReconstructionBatch(const EventBatch&)`。
- [ ] **Step 1: Tests.** 有效局部颜料/路径、嵌图、sourceHandle引用、整张residual操作、错误对象ID、旧revision、取消。
```cpp
CHECK(source_removed_then_replay.pixel_hash == normal_replay.pixel_hash);
CHECK(bitmap_replacement_event.rejected);
CHECK(unbounded_residual_operation.rejected);
CHECK(cancelled_compile.document_revision == revision_before);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E01 -Phase Red`；断言失败，环境/模型不足为Blocked。
- [ ] **Step 3: Implement.** Compiler独享只读源图句柄，输出路径/压力/颜料/对象/层/颜色空间/版本/种子。播放进程不挂源图资源；校正必须局部事件。输出到staging，validate+inputRevision匹配后一次提交。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E01 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E01 reconstructable event provenance"`；仅stage指定文件。
**Accept:** 拿走输入位图和模型后，既存重建作品仍可从事件完整复原。

### Task E02: 连续起稿、分区铺色与落笔调度
**Dependencies:** E01, R03。
**Files:** Create `src/reconstruct/ContourPlanner.cpp`, `src/reconstruct/RegionPlanner.cpp`, `src/reconstruct/StrokeScheduler.cpp`; Test `tests/reconstruct/PlanningTests.cpp`。
**Interfaces:** ConsumesPlanningRegion/StyleSettings/EventBatch/SchedulePolicy；Produces `Result<EventBatch> planContours(const PlanningRegion&)`, `Result<EventBatch> planBasePigment(const PlanningRegion&, const StyleSettings&)`, `Result<EventBatch> scheduleStrokes(const EventBatch&, const SchedulePolicy&)`。
- [ ] **Step 1: Tests.** 实心/透明/文字细边/包装折面；定义邻接区域图与精确保护mask，局部底色→中色先后可断言。
```cpp
CHECK(strokes_outside_region_mask == 0);
CHECK(mid_pass.before_local_base == 0);
CHECK(same_inputs_seed.events_hash == rerun.events_hash);
CHECK(early_phase.disconnected_random_scribbles == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E02 -Phase Red`。
- [ ] **Step 3: Implement.** 先连续结构/主轮廓后局部颜料；按对象邻接完成一个区域再换区。笔触方向跟结构梯度，保留白反光与对象边界；不为“自然”改字形、轮廓或添加全幅随机抖动。shape复杂不强制八层模板。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E02 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E02 coherent drawing plans"`。
**Accept:** 0.5x可解释当前新增哪一笔/哪一区，起稿不是噪声云。

### Task E03: 误差驱动局部精修与预算
**Dependencies:** E02, C02。
**Files:** Create `src/reconstruct/PigmentRefiner.cpp`, `src/reconstruct/TexturePlanner.cpp`, `src/reconstruct/RegionBudget.cpp`; Test `tests/reconstruct/RefinementTests.cpp`。
**Interfaces:** ConsumesRefinementRequest/QualityBudget/ReferenceRenderer；Produces `Result<EventBatch> refineRegion(const RefinementRequest&, std::stop_token)`, `QualityProgress measureRegionProgress(const RefinementRequest&)`。
- [ ] **Step 1: Tests.** 透明边/红色细褶/发丝/平滑渐变、极小event-budget；禁改非选中对象，预算耗尽仍未达标必须返回NeedsReview。
```cpp
CHECK(refine_object_a.other_object_hash == original_other_hash);
CHECK(events_are_bounded_solid_pigment_not_residual_image);
CHECK(exhausted_budget.quality_status == QualityStatus::NeedsReview);
CHECK(progress.complete_claim_requires_all_requested_work);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E03 -Phase Red`。
- [ ] **Step 3: Implement.** coarse→fine按局部误差分配预算；透明边coverage/颜色独立优化，CMYK在原文档空间颜料值上处理。纹理由短方向性笔迹产生；不全图逐像素打印伪装真人。预算/超时可增配或修mask，不偷偷缩图/糊图/换图。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E03 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E03 bounded local refinement"`。
**Accept:** 质量有实际误差与剩余工作解释，不用进度百分比当画完。

### Task E04: 文字/Logo关键区域与明确可选文本候选
**Dependencies:** E03, C04。
**Files:** Create `src/reconstruct/CriticalRegions.cpp`, `src/text/TextCandidateService.cpp`; Test `tests/reconstruct/TextLogoTests.cpp`。
**Interfaces:** ConsumesCriticalRegionRequest/TextCandidateRequest；Produces `QualityReport checkCriticalRegions(const CriticalRegionRequest&)`, `Result<TextCandidates> suggestText(const TextCandidateRequest&, std::stop_token)`。
- [ ] **Step 1: Tests.** 弯曲包装Logo、小号中文/拉丁字、缺字体、用户未请求OCR、已有内容标记。人工ROI和边缘误差独立于全图评分。
```cpp
CHECK(default_reconstruction.ocr_calls == 0);
CHECK(fidelity.logo_font_substitutions == 0);
CHECK(text_suggestion.without_explicit_consent.rejected);
CHECK(psd_export.claimed_editable_text_layers == 0);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E04 -Phase Red`。
- [ ] **Step 3: Implement.** 字体替换永不进入默认Fidelity；轮廓路径与像素形状优先。用户主动请求且需要文本编辑才启用本地可选候选；展示缺字体/候选、不自动改图。工具不添加水印，不移除源图标记、不伪造作者历史。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E04 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E04 critical text and logo fidelity"`。
**Accept:** 关键小区域可单独判失败，不能被大面积背景拉高分掩盖。

### Task E05: 五种风格与版本化重建变体
**Dependencies:** E03, R04。
**Files:** Create `src/reconstruct/StyleRegistry.cpp`, `resources/styles/fidelity.json`, `resources/styles/dry-media.json`, `resources/styles/marker.json`, `resources/styles/pencil.json`, `resources/styles/concept-sketch.json`; Test `tests/reconstruct/StyleTests.cpp`。
**Interfaces:** ConsumesStyleId/StyleSettings/SnapshotRef；Produces `Result<StyleSettings> StyleRegistry::load(StyleId)`, `Result<ReconstructionRequest> makeStyleVariant(SnapshotRef, StyleId)`。
- [ ] **Step 1: Tests.** 五风格相同对象ID/蒙版；变体不覆盖原工程，Fidelity没有默认风格滤镜；保存后无模型可复原。
```cpp
CHECK(variant.object_ids == source.object_ids);
CHECK(fidelity.automatic_stylization == false);
CHECK(style_change.original_snapshot_hash_unchanged);
CHECK(replay_without_models.hash == saved_replay_hash);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E05 -Phase Red`。
- [ ] **Step 3: Implement.** 每种媒体明确coverage/pressure/grain/调度参数，版本与seed写工程；渲染帧不重新抽随机数。风格版本独立保存，保留对象结构；风格偏离单列，不套Fidelity分数蒙混。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E05 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E05 versioned reconstruction styles"`。
**Accept:** 五个真实重建路径，不是五个改名按钮。

### Task E06: 完整重绘纵向链与三重验收
**Dependencies:** E04, E05, C05, C06, R04, M06, G01。
**Files:** Create `src/quality/QualityEvaluator.h`, `src/quality/QualityEvaluator.cpp`, `tools/qa/quality_runner.cpp`; Test `tests/integration/ReconstructionExportTests.cpp`。
**Interfaces:** ConsumesQualityRequest/QualityReport/HumanReview；Produces `Result<QualityReport> QualityEvaluator::evaluate(const QualityRequest&)`, `QualityDecision acceptQuality(const QualityReport&, const HumanReview&)`。
- [ ] **Step 1: Tests.** 先512级产品/人物/海报/透明图纵向验证，再全分辨率；将坏自然度/错对象/错文字各作为负例。
```cpp
CHECK(fidelity_metrics.ssim >= 0.98);
CHECK(fidelity_metrics.mean_delta_e00 <= 2 && fidelity_metrics.p95_delta_e00 <= 5);
CHECK(alpha_mae <= 2.0/255.0);
CHECK(good_pixels_bad_process.accepted == false);
CHECK(saved_reopened_psd_layers == reviewed_layers);
```
- [ ] **Step 2: RED.** `powershell -File tools/test-task.ps1 -Id E06 -Phase Red`；未有真实模型/PS或人审为Blocked。
- [ ] **Step 3: Implement.** 输入→Review→重建→局部修改→回放→PSD/PNG→重开一条真实链；统一ICC比较、ROI不任意排除、mask/对象隔离、人审自然度三独立门。导出和重开不依赖源图或AI重新分析；实测质量失败明确报告，不以截图demo代替。
- [ ] **Step 4: GREEN.** `powershell -File tools/test-task.ps1 -Id E06 -Phase Green`; `ctest --preset win-debug --no-tests=error --output-on-failure`；附CPU/GPU/Photoshop/人工分列证据。
- [ ] **Step 5: Review/commit.** `git commit -m "feat: E06 verified reconstruction pipeline"`。
**Accept:** 此门没过，不把后续漂亮UI标成核心产品已完成。
